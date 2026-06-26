#include "pal/pal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/wait.h>
  #include <errno.h>
  #include <signal.h>
  #include <poll.h>
  #include <time.h>
#endif

// Default timeout for child processes (2 minutes)
#define PAL_DEFAULT_TIMEOUT_MS 120000

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#ifdef _WIN32

/// Build a command line string from program and arguments, with proper quoting.
/// Windows CreateProcess requires a single command line string.
/// Returns a malloc'd string the caller must free.
static char* build_command_line(const char* program, const char** args, int arg_count) {
    // Calculate total length needed
    size_t total_len = 0;

    // Account for program (quoted)
    total_len += strlen(program) + 3; // quotes + space

    for (int i = 0; i < arg_count; i++) {
        // Each arg: quotes + content + closing quote + space
        total_len += strlen(args[i]) * 2 + 3; // worst case: every char needs escaping
    }

    total_len += 1; // null terminator

    char* cmd = (char*)malloc(total_len);
    if (!cmd) return NULL;

    char* p = cmd;

    // Quote the program path
    *p++ = '"';
    size_t prog_len = strlen(program);
    memcpy(p, program, prog_len);
    p += prog_len;
    *p++ = '"';

    // Append each argument, quoted
    for (int i = 0; i < arg_count; i++) {
        *p++ = ' ';
        *p++ = '"';
        const char* arg = args[i];
        while (*arg) {
            if (*arg == '"') {
                *p++ = '\\';
                *p++ = '"';
            } else if (*arg == '\\') {
                // Check if this backslash is followed by a quote
                const char* next = arg + 1;
                int num_backslashes = 1;
                while (*next == '\\') {
                    num_backslashes++;
                    next++;
                }
                if (*next == '"' || *next == '\0') {
                    // Double the backslashes before a quote or at end
                    for (int j = 0; j < num_backslashes * 2; j++) {
                        *p++ = '\\';
                    }
                    arg = next - 1; // skip past extra backslashes (loop increments)
                } else {
                    *p++ = *arg;
                }
            } else {
                *p++ = *arg;
            }
            arg++;
        }
        *p++ = '"';
    }

    *p = '\0';
    return cmd;
}

/// Build a null-terminated environment block from an array of "KEY=VALUE" strings.
/// Windows requires environment as a single block with each var null-terminated
/// and the entire block double-null-terminated.
static char* build_env_block(const char** env) {
    if (!env) return NULL;

    // Calculate total size
    size_t total = 0;
    int count = 0;
    while (env[count]) {
        total += strlen(env[count]) + 1; // string + null
        count++;
    }
    total += 1; // final double-null

    char* block = (char*)malloc(total);
    if (!block) return NULL;

    char* p = block;
    for (int i = 0; i < count; i++) {
        size_t len = strlen(env[i]);
        memcpy(p, env[i], len);
        p += len;
        *p++ = '\0';
    }
    *p = '\0'; // double-null terminator

    return block;
}

/// Context passed to each pipe reader thread.
typedef struct {
    HANDLE pipe;      // read-end of the pipe (thread closes it when done)
    char*  buf;       // output: malloc'd null-terminated buffer (caller frees)
    size_t buf_len;   // output: number of bytes read (excluding null terminator)
    DWORD  error;     // 0 on success, otherwise GetLastError() value
} PipeReaderCtx;

/// Resolve the effective timeout in milliseconds for WaitForMultipleObjects.
/// 0 = use default (PAL_DEFAULT_TIMEOUT_MS), -1 = INFINITE, >0 = use as-is.
static DWORD resolve_timeout_ms(int timeout_ms) {
    if (timeout_ms < 0) return INFINITE;
    if (timeout_ms == 0) return PAL_DEFAULT_TIMEOUT_MS;
    return (DWORD)timeout_ms;
}

/// Thread function: reads a pipe to completion into a malloc'd buffer.
static DWORD WINAPI pipe_reader_thread(LPVOID param) {
    PipeReaderCtx* ctx = (PipeReaderCtx*)param;
    size_t capacity = 4096;
    size_t size = 0;
    char* buf = (char*)malloc(capacity);
    if (!buf) {
        ctx->buf = NULL;
        ctx->buf_len = 0;
        ctx->error = ERROR_NOT_ENOUGH_MEMORY;
        CloseHandle(ctx->pipe);
        return 1;
    }

    DWORD bytes_read;
    for (;;) {
        BOOL ok = ReadFile(ctx->pipe, buf + size, (DWORD)(capacity - size - 1), &bytes_read, NULL);
        if (!ok || bytes_read == 0) break;
        size += bytes_read;
        if (size + 1 >= capacity) {
            capacity *= 2;
            char* new_buf = (char*)realloc(buf, capacity);
            if (!new_buf) {
                free(buf);
                ctx->buf = NULL;
                ctx->buf_len = 0;
                ctx->error = ERROR_NOT_ENOUGH_MEMORY;
                CloseHandle(ctx->pipe);
                return 1;
            }
            buf = new_buf;
        }
    }

    buf[size] = '\0';
    ctx->buf = buf;
    ctx->buf_len = size;
    ctx->error = 0;
    CloseHandle(ctx->pipe);
    return 0;
}

#else // POSIX

/// Read all data from a file descriptor into a malloc'd buffer.
static char* read_fd_to_buf(int fd) {
    size_t capacity = 4096;
    size_t size = 0;
    char* buf = (char*)malloc(capacity);
    if (!buf) return NULL;

    ssize_t n;
    while ((n = read(fd, buf + size, capacity - size - 1)) > 0) {
        size += (size_t)n;
        if (size + 1 >= capacity) {
            capacity *= 2;
            char* new_buf = (char*)realloc(buf, capacity);
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }
    }

    buf[size] = '\0';
    return buf;
}

/// Read from two file descriptors concurrently using poll().
/// Grows dynamic buffers as data arrives from both pipes.
/// If timeout_ms > 0, kills the child process after that many milliseconds.
/// timeout_ms: 0 = use default (PAL_DEFAULT_TIMEOUT_MS), -1 = infinite, >0 = use as-is.
/// Returns 0 on success, PAL_ERR_TIMEOUT on timeout, PAL_ERR_IO on failure.
static int read_pipes_concurrent(int fd_out, int fd_err, char** buf_out, char** buf_err,
                                 int timeout_ms, pid_t child_pid) {
    size_t cap_out = 4096, size_out = 0;
    size_t cap_err = 4096, size_err = 0;

    char* out = (char*)malloc(cap_out);
    char* err = (char*)malloc(cap_err);
    if (!out || !err) {
        free(out);
        free(err);
        return PAL_ERR_IO;
    }

    // Resolve effective timeout
    int effective_timeout_ms;
    if (timeout_ms < 0) {
        effective_timeout_ms = -1; // infinite
    } else if (timeout_ms == 0) {
        effective_timeout_ms = PAL_DEFAULT_TIMEOUT_MS;
    } else {
        effective_timeout_ms = timeout_ms;
    }

    // Get start time for timeout tracking
    struct timespec start_time;
    if (effective_timeout_ms > 0) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }

    struct pollfd fds[2];
    fds[0].fd = fd_out;
    fds[0].events = POLLIN;
    fds[1].fd = fd_err;
    fds[1].events = POLLIN;

    int active = 2;
    int timed_out = 0;

    while (active > 0) {
        // Compute remaining time for poll
        int poll_timeout = -1; // infinite by default
        if (effective_timeout_ms > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (now.tv_sec - start_time.tv_sec) * 1000 +
                              (now.tv_nsec - start_time.tv_nsec) / 1000000;
            long remaining = effective_timeout_ms - elapsed_ms;
            if (remaining <= 0) {
                timed_out = 1;
                break;
            }
            poll_timeout = (int)remaining;
        }

        int ret = poll(fds, 2, poll_timeout);
        if (ret < 0) {
            if (errno == EINTR) continue;
            free(out);
            free(err);
            return PAL_ERR_IO;
        }

        if (ret == 0) {
            // poll timed out
            timed_out = 1;
            break;
        }

        /* Check stdout pipe */
        if (fds[0].fd >= 0 && (fds[0].revents & (POLLIN | POLLHUP))) {
            ssize_t n = read(fds[0].fd, out + size_out, cap_out - size_out - 1);
            if (n > 0) {
                size_out += (size_t)n;
                if (size_out + 1 >= cap_out) {
                    cap_out *= 2;
                    char* tmp = (char*)realloc(out, cap_out);
                    if (!tmp) { free(out); free(err); return PAL_ERR_IO; }
                    out = tmp;
                }
            } else {
                /* EOF or error on stdout */
                fds[0].fd = -1;
                active--;
            }
        }

        /* Check stderr pipe */
        if (fds[1].fd >= 0 && (fds[1].revents & (POLLIN | POLLHUP))) {
            ssize_t n = read(fds[1].fd, err + size_err, cap_err - size_err - 1);
            if (n > 0) {
                size_err += (size_t)n;
                if (size_err + 1 >= cap_err) {
                    cap_err *= 2;
                    char* tmp = (char*)realloc(err, cap_err);
                    if (!tmp) { free(out); free(err); return PAL_ERR_IO; }
                    err = tmp;
                }
            } else {
                /* EOF or error on stderr */
                fds[1].fd = -1;
                active--;
            }
        }
    }

    if (timed_out) {
        // Kill the child process
        kill(child_pid, SIGKILL);

        // Drain remaining pipe data (child is dead, pipes will EOF soon)
        // Set short poll timeout to drain without blocking forever
        while (active > 0) {
            int ret = poll(fds, 2, 100); // 100ms drain timeout
            if (ret <= 0) break;
            if (fds[0].fd >= 0 && (fds[0].revents & (POLLIN | POLLHUP))) {
                ssize_t n = read(fds[0].fd, out + size_out, cap_out - size_out - 1);
                if (n > 0) {
                    size_out += (size_t)n;
                    if (size_out + 1 >= cap_out) {
                        cap_out *= 2;
                        char* tmp = (char*)realloc(out, cap_out);
                        if (!tmp) break;
                        out = tmp;
                    }
                } else { fds[0].fd = -1; active--; }
            }
            if (fds[1].fd >= 0 && (fds[1].revents & (POLLIN | POLLHUP))) {
                ssize_t n = read(fds[1].fd, err + size_err, cap_err - size_err - 1);
                if (n > 0) {
                    size_err += (size_t)n;
                    if (size_err + 1 >= cap_err) {
                        cap_err *= 2;
                        char* tmp = (char*)realloc(err, cap_err);
                        if (!tmp) break;
                        err = tmp;
                    }
                } else { fds[1].fd = -1; active--; }
            }
        }
    }

    out[size_out] = '\0';
    err[size_err] = '\0';

    *buf_out = out;
    *buf_err = err;
    return timed_out ? PAL_ERR_TIMEOUT : 0;
}

#endif

// ---------------------------------------------------------------------------
// pal_spawn - Synchronous process execution
// ---------------------------------------------------------------------------

int pal_spawn(const PalSpawnOpts* opts, PalSpawnResult* result) {
    if (!opts || !opts->program) return PAL_ERR_IO;

    if (result) {
        result->exit_code = -1;
        result->stdout_buf = NULL;
        result->stderr_buf = NULL;
    }

#ifdef _WIN32
    HANDLE stdout_read = NULL, stdout_write = NULL;
    HANDLE stderr_read = NULL, stderr_write = NULL;

    if (opts->capture_output) {
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) return PAL_ERR_IO;
        if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            return PAL_ERR_IO;
        }

        if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            return PAL_ERR_IO;
        }
        if (!SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stderr_read);
            CloseHandle(stderr_write);
            return PAL_ERR_IO;
        }
    }

    char* cmd_line = build_command_line(opts->program, opts->args, opts->arg_count);
    if (!cmd_line) {
        if (opts->capture_output) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stderr_read);
            CloseHandle(stderr_write);
        }
        return PAL_ERR_IO;
    }

    char* env_block = build_env_block(opts->env);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    if (opts->capture_output) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = stdout_write;
        si.hStdError = stderr_write;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }

    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessA(
        NULL,           // lpApplicationName
        cmd_line,       // lpCommandLine
        NULL,           // lpProcessAttributes
        NULL,           // lpThreadAttributes
        opts->capture_output ? TRUE : FALSE, // bInheritHandles
        0,              // dwCreationFlags
        env_block,      // lpEnvironment (NULL = inherit)
        opts->cwd,      // lpCurrentDirectory (NULL = inherit)
        &si,
        &pi
    );

    free(cmd_line);
    free(env_block);

    if (!success) {
        if (opts->capture_output) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stderr_read);
            CloseHandle(stderr_write);
        }
        return PAL_ERR_IO;
    }

    // Close write ends of pipes - the child owns them now
    if (opts->capture_output) {
        CloseHandle(stdout_write);
        CloseHandle(stderr_write);
    }

    // Read captured output concurrently using reader threads
    if (opts->capture_output && result) {
        PipeReaderCtx stdout_ctx = { stdout_read, NULL, 0, 0 };
        PipeReaderCtx stderr_ctx = { stderr_read, NULL, 0, 0 };

        HANDLE hStdoutThread = CreateThread(NULL, 0, pipe_reader_thread, &stdout_ctx, 0, NULL);
        HANDLE hStderrThread = CreateThread(NULL, 0, pipe_reader_thread, &stderr_ctx, 0, NULL);

        if (!hStdoutThread || !hStderrThread) {
            // Thread creation failed - fall back to closing pipes and waiting
            if (hStdoutThread) {
                WaitForSingleObject(hStdoutThread, INFINITE);
                CloseHandle(hStdoutThread);
            } else {
                CloseHandle(stdout_read);
            }
            if (hStderrThread) {
                WaitForSingleObject(hStderrThread, INFINITE);
                CloseHandle(hStderrThread);
            } else {
                CloseHandle(stderr_read);
            }
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exit_code = 0;
            GetExitCodeProcess(pi.hProcess, &exit_code);
            result->exit_code = (int)exit_code;
            result->stdout_buf = stdout_ctx.buf;
            result->stderr_buf = stderr_ctx.buf;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return PAL_OK;
        }

        // Wait for process and both reader threads to complete
        HANDLE wait_handles[3] = { pi.hProcess, hStdoutThread, hStderrThread };
        DWORD timeout = resolve_timeout_ms(opts->timeout_ms);
        DWORD wait_result = WaitForMultipleObjects(3, wait_handles, TRUE, timeout);

        if (wait_result == WAIT_TIMEOUT) {
            // Timeout expired - terminate the child process
            TerminateProcess(pi.hProcess, 1);

            // Wait for threads to finish (they'll get broken pipe after termination)
            WaitForMultipleObjects(2, &wait_handles[1], TRUE, 5000);

            // Collect any partial buffers from thread contexts
            result->stdout_buf = stdout_ctx.buf;
            result->stderr_buf = stderr_ctx.buf;

            CloseHandle(hStdoutThread);
            CloseHandle(hStderrThread);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return PAL_ERR_TIMEOUT;
        }

        // Collect buffers from thread contexts
        result->stdout_buf = stdout_ctx.buf;
        result->stderr_buf = stderr_ctx.buf;

        CloseHandle(hStdoutThread);
        CloseHandle(hStderrThread);
    } else if (opts->capture_output) {
        // No result struct - just close pipes and wait with timeout
        CloseHandle(stdout_read);
        CloseHandle(stderr_read);
        DWORD timeout = resolve_timeout_ms(opts->timeout_ms);
        DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout);
        if (wait_result == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return PAL_ERR_TIMEOUT;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return PAL_OK;
    } else {
        // No output capture - just wait for process with timeout
        DWORD timeout = resolve_timeout_ms(opts->timeout_ms);
        DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout);
        if (wait_result == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return PAL_ERR_TIMEOUT;
        }
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    if (result) {
        result->exit_code = (int)exit_code;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return PAL_OK;

#else // POSIX
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (opts->capture_output) {
        if (pipe(stdout_pipe) != 0) return PAL_ERR_IO;
        if (pipe(stderr_pipe) != 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            return PAL_ERR_IO;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (opts->capture_output) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        return PAL_ERR_IO;
    }

    if (pid == 0) {
        // Child process
        if (opts->capture_output) {
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
        }

        if (opts->cwd) {
            if (chdir(opts->cwd) != 0) {
                _exit(127);
            }
        }

        // Set environment variables if specified
        if (opts->env) {
            // Clear and set new environment
            // env is expected as array of "KEY=VALUE" strings, NULL terminated
            extern char** environ;
            int env_count = 0;
            while (opts->env[env_count]) env_count++;

            // Build new environ array
            char** new_env = (char**)malloc((env_count + 1) * sizeof(char*));
            if (!new_env) _exit(127);
            for (int i = 0; i < env_count; i++) {
                new_env[i] = (char*)opts->env[i];
            }
            new_env[env_count] = NULL;
            environ = new_env;
        }

        // Build argv array: program + args + NULL
        int total_args = 1 + opts->arg_count + 1;
        char** argv = (char**)malloc(total_args * sizeof(char*));
        if (!argv) _exit(127);

        argv[0] = (char*)opts->program;
        for (int i = 0; i < opts->arg_count; i++) {
            argv[i + 1] = (char*)opts->args[i];
        }
        argv[1 + opts->arg_count] = NULL;

        execvp(opts->program, argv);
        // If execvp returns, it failed
        _exit(127);
    }

    // Parent process
    if (opts->capture_output) {
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        int pipe_result = 0;
        if (result) {
            pipe_result = read_pipes_concurrent(stdout_pipe[0], stderr_pipe[0],
                                  &result->stdout_buf, &result->stderr_buf,
                                  opts->timeout_ms, pid);
        }

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        // If timeout occurred, reap the child and return timeout error
        if (pipe_result == PAL_ERR_TIMEOUT) {
            int status = 0;
            waitpid(pid, &status, 0);
            if (result) {
                if (WIFEXITED(status)) {
                    result->exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    result->exit_code = 128 + WTERMSIG(status);
                } else {
                    result->exit_code = -1;
                }
            }
            return PAL_ERR_TIMEOUT;
        }
    }

    int status = 0;

    // If not capturing output, we still need timeout support
    if (!opts->capture_output) {
        int effective_timeout_ms;
        if (opts->timeout_ms < 0) {
            effective_timeout_ms = -1; // infinite
        } else if (opts->timeout_ms == 0) {
            effective_timeout_ms = PAL_DEFAULT_TIMEOUT_MS;
        } else {
            effective_timeout_ms = opts->timeout_ms;
        }

        if (effective_timeout_ms > 0) {
            struct timespec start_time;
            clock_gettime(CLOCK_MONOTONIC, &start_time);

            while (1) {
                pid_t w = waitpid(pid, &status, WNOHANG);
                if (w > 0) break; // child exited
                if (w < 0) return PAL_ERR_IO;

                // Check timeout
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                long elapsed_ms = (now.tv_sec - start_time.tv_sec) * 1000 +
                                  (now.tv_nsec - start_time.tv_nsec) / 1000000;
                if (elapsed_ms >= effective_timeout_ms) {
                    kill(pid, SIGKILL);
                    waitpid(pid, &status, 0);
                    if (result) {
                        if (WIFSIGNALED(status)) {
                            result->exit_code = 128 + WTERMSIG(status);
                        } else {
                            result->exit_code = -1;
                        }
                    }
                    return PAL_ERR_TIMEOUT;
                }

                // Sleep briefly to avoid busy-waiting (10ms)
                struct timespec sleep_ts = { 0, 10 * 1000000 };
                nanosleep(&sleep_ts, NULL);
            }
        } else {
            waitpid(pid, &status, 0);
        }
    } else {
        waitpid(pid, &status, 0);
    }

    if (result) {
        if (WIFEXITED(status)) {
            result->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result->exit_code = 128 + WTERMSIG(status);
        } else {
            result->exit_code = -1;
        }
    }

    return PAL_OK;
#endif
}

// ---------------------------------------------------------------------------
// pal_spawn_async - Launch process without waiting
// ---------------------------------------------------------------------------

int pal_spawn_async(const PalSpawnOpts* opts, int* pid_out) {
    if (!opts || !opts->program || !pid_out) return PAL_ERR_IO;

#ifdef _WIN32
    char* cmd_line = build_command_line(opts->program, opts->args, opts->arg_count);
    if (!cmd_line) return PAL_ERR_IO;

    char* env_block = build_env_block(opts->env);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessA(
        NULL,
        cmd_line,
        NULL,
        NULL,
        FALSE,
        0,
        env_block,
        opts->cwd,
        &si,
        &pi
    );

    free(cmd_line);
    free(env_block);

    if (!success) return PAL_ERR_IO;

    // Store the process handle as the "pid" for later use in pal_wait.
    // On Windows, we use the process ID and keep the handle open via a
    // simple mapping. For simplicity, store the actual process ID and
    // keep the handle associated. We'll re-open it in pal_wait if needed.
    // Actually, we need the handle to wait on it. Store handle cast to int.
    // This works because HANDLE values fit in an intptr_t.
    *pid_out = (int)(intptr_t)pi.hProcess;

    // Close the thread handle - we don't need it
    CloseHandle(pi.hThread);

    return PAL_OK;

#else // POSIX
    pid_t pid = fork();
    if (pid < 0) return PAL_ERR_IO;

    if (pid == 0) {
        // Child process
        if (opts->cwd) {
            if (chdir(opts->cwd) != 0) {
                _exit(127);
            }
        }

        if (opts->env) {
            extern char** environ;
            int env_count = 0;
            while (opts->env[env_count]) env_count++;

            char** new_env = (char**)malloc((env_count + 1) * sizeof(char*));
            if (!new_env) _exit(127);
            for (int i = 0; i < env_count; i++) {
                new_env[i] = (char*)opts->env[i];
            }
            new_env[env_count] = NULL;
            environ = new_env;
        }

        int total_args = 1 + opts->arg_count + 1;
        char** argv = (char**)malloc(total_args * sizeof(char*));
        if (!argv) _exit(127);

        argv[0] = (char*)opts->program;
        for (int i = 0; i < opts->arg_count; i++) {
            argv[i + 1] = (char*)opts->args[i];
        }
        argv[1 + opts->arg_count] = NULL;

        execvp(opts->program, argv);
        _exit(127);
    }

    *pid_out = (int)pid;
    return PAL_OK;
#endif
}

// ---------------------------------------------------------------------------
// pal_wait - Wait for an async process to complete
// ---------------------------------------------------------------------------

int pal_wait(int pid, int* exit_code) {
    if (!exit_code) return PAL_ERR_IO;

#ifdef _WIN32
    HANDLE hProcess = (HANDLE)(intptr_t)pid;

    DWORD wait_result = WaitForSingleObject(hProcess, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        CloseHandle(hProcess);
        return PAL_ERR_IO;
    }

    DWORD code = 0;
    if (!GetExitCodeProcess(hProcess, &code)) {
        CloseHandle(hProcess);
        return PAL_ERR_IO;
    }

    *exit_code = (int)code;
    CloseHandle(hProcess);
    return PAL_OK;

#else // POSIX
    int status = 0;
    pid_t result = waitpid((pid_t)pid, &status, 0);
    if (result < 0) return PAL_ERR_IO;

    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *exit_code = 128 + WTERMSIG(status);
    } else {
        *exit_code = -1;
    }

    return PAL_OK;
#endif
}

// ---------------------------------------------------------------------------
// pal_spawn_result_free - Free captured output buffers
// ---------------------------------------------------------------------------

void pal_spawn_result_free(PalSpawnResult* result) {
    if (!result) return;
    free(result->stdout_buf);
    free(result->stderr_buf);
    result->stdout_buf = NULL;
    result->stderr_buf = NULL;
}
