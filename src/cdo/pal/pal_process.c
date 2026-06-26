#include "pal.h"

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
#endif

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

/// Read all data from a pipe handle into a malloc'd buffer.
static char* read_pipe_to_buf(HANDLE pipe) {
    size_t capacity = 4096;
    size_t size = 0;
    char* buf = (char*)malloc(capacity);
    if (!buf) return NULL;

    DWORD bytes_read;
    while (ReadFile(pipe, buf + size, (DWORD)(capacity - size - 1), &bytes_read, NULL) && bytes_read > 0) {
        size += bytes_read;
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

    // Read captured output
    if (opts->capture_output && result) {
        result->stdout_buf = read_pipe_to_buf(stdout_read);
        result->stderr_buf = read_pipe_to_buf(stderr_read);
        CloseHandle(stdout_read);
        CloseHandle(stderr_read);
    } else if (opts->capture_output) {
        CloseHandle(stdout_read);
        CloseHandle(stderr_read);
    }

    // Wait for process to complete
    WaitForSingleObject(pi.hProcess, INFINITE);

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

        if (result) {
            result->stdout_buf = read_fd_to_buf(stdout_pipe[0]);
            result->stderr_buf = read_fd_to_buf(stderr_pipe[0]);
        }

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
    }

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
