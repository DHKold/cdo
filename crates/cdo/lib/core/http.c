#include "core/http.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <winhttp.h>
  #pragma comment(lib, "winhttp.lib")
#else
  #include <unistd.h>
  #include <time.h>
#endif

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Platform-aware sleep in milliseconds.
static void http_sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/// Set an error message on an HttpResponse (allocates a copy).
static void http_set_error(HttpResponse* resp, const char* msg) {
    if (!resp) return;
    free(resp->error_msg);
    resp->error_msg = msg ? strdup(msg) : NULL;
}

// ---------------------------------------------------------------------------
// URL parsing (minimal, for WinHTTP)
// ---------------------------------------------------------------------------

#ifdef _WIN32

typedef struct {
    char host[256];
    char path[1024];
    int  port;
    int  use_tls;
} ParsedUrl;

/// Parse an HTTP(S) URL into host, path, port, and TLS flag.
/// Returns 0 on success, -1 on failure.
static int parse_url(const char* url, ParsedUrl* out) {
    memset(out, 0, sizeof(*out));

    if (strncmp(url, "https://", 8) == 0) {
        out->use_tls = 1;
        out->port = 443;
        url += 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        out->use_tls = 0;
        out->port = 80;
        url += 7;
    } else {
        return -1;
    }

    // Find the end of the host (either '/', ':', or end of string)
    const char* host_end = url;
    while (*host_end && *host_end != '/' && *host_end != ':') {
        host_end++;
    }

    size_t host_len = (size_t)(host_end - url);
    if (host_len == 0 || host_len >= sizeof(out->host)) return -1;
    memcpy(out->host, url, host_len);
    out->host[host_len] = '\0';

    // Optional port
    if (*host_end == ':') {
        host_end++;
        out->port = atoi(host_end);
        while (*host_end && *host_end != '/') host_end++;
    }

    // Path (default to "/" if none)
    if (*host_end == '/') {
        size_t path_len = strlen(host_end);
        if (path_len >= sizeof(out->path)) return -1;
        memcpy(out->path, host_end, path_len);
        out->path[path_len] = '\0';
    } else {
        out->path[0] = '/';
        out->path[1] = '\0';
    }

    return 0;
}

/// Convert a narrow (UTF-8/ASCII) string to a wide string. Caller must free.
static wchar_t* to_wide(const char* s) {
    if (!s) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t* ws = (wchar_t*)malloc((size_t)len * sizeof(wchar_t));
    if (!ws) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, len);
    return ws;
}

// ---------------------------------------------------------------------------
// Windows WinHTTP implementation
// ---------------------------------------------------------------------------

/// Single download attempt using WinHTTP. Returns 0 on success.
static int winhttp_download_attempt(const char* url, const char* dest_path,
                                    int* out_status, HttpProgressFunc progress, void* ctx) {
    ParsedUrl pu;
    if (parse_url(url, &pu) != 0) return -1;

    wchar_t* w_host = to_wide(pu.host);
    wchar_t* w_path = to_wide(pu.path);
    if (!w_host || !w_path) {
        free(w_host);
        free(w_path);
        return -1;
    }

    HINTERNET hSession = WinHttpOpen(L"CDo/0.1",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        free(w_host);
        free(w_path);
        return -1;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, w_host, (INTERNET_PORT)pu.port, 0);
    free(w_host);
    if (!hConnect) {
        free(w_path);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD flags = pu.use_tls ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", w_path,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    free(w_path);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    // Send request
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    // Receive response
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    // Check status code
    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (out_status) *out_status = (int)status_code;

    if (status_code < 200 || status_code >= 300) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    // Get content length for progress reporting
    size_t total_size = 0;
    wchar_t content_len_buf[64] = {0};
    DWORD cl_size = sizeof(content_len_buf);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, content_len_buf,
                            &cl_size, WINHTTP_NO_HEADER_INDEX)) {
        total_size = (size_t)_wtoi64(content_len_buf);
    }

    // Open destination file
    FILE* fp = fopen(dest_path, "wb");
    if (!fp) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    // Read data in chunks
    size_t downloaded = 0;
    DWORD bytes_available = 0;
    int result = 0;

    for (;;) {
        if (!WinHttpQueryDataAvailable(hRequest, &bytes_available)) {
            result = -1;
            break;
        }
        if (bytes_available == 0) break;

        // Cap read buffer at 64KB
        DWORD to_read = bytes_available > 65536 ? 65536 : bytes_available;
        char* buf = (char*)malloc(to_read);
        if (!buf) {
            result = -1;
            break;
        }

        DWORD bytes_read = 0;
        if (!WinHttpReadData(hRequest, buf, to_read, &bytes_read)) {
            free(buf);
            result = -1;
            break;
        }

        if (bytes_read > 0) {
            if (fwrite(buf, 1, bytes_read, fp) != bytes_read) {
                free(buf);
                result = -1;
                break;
            }
            downloaded += bytes_read;

            if (progress) {
                progress(downloaded, total_size, ctx);
            }
        }

        free(buf);

        if (bytes_read == 0) break;
    }

    fclose(fp);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // If download failed partway, remove partial file
    if (result != 0) {
        DeleteFileA(dest_path);
    }

    return result;
}

/// Single GET-to-memory attempt using WinHTTP. Returns 0 on success.
static int winhttp_get_attempt(const char* url, HttpResponse* resp) {
    ParsedUrl pu;
    if (parse_url(url, &pu) != 0) {
        http_set_error(resp, "Failed to parse URL");
        return -1;
    }

    wchar_t* w_host = to_wide(pu.host);
    wchar_t* w_path = to_wide(pu.path);
    if (!w_host || !w_path) {
        free(w_host);
        free(w_path);
        http_set_error(resp, "Memory allocation failure");
        return -1;
    }

    HINTERNET hSession = WinHttpOpen(L"CDo/0.1",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        free(w_host);
        free(w_path);
        http_set_error(resp, "WinHTTP session init failed");
        return -1;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, w_host, (INTERNET_PORT)pu.port, 0);
    free(w_host);
    if (!hConnect) {
        free(w_path);
        WinHttpCloseHandle(hSession);
        http_set_error(resp, "WinHTTP connect failed");
        return -1;
    }

    DWORD flags = pu.use_tls ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", w_path,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    free(w_path);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        http_set_error(resp, "WinHTTP open request failed");
        return -1;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        http_set_error(resp, "WinHTTP send request failed");
        return -1;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        http_set_error(resp, "WinHTTP receive response failed");
        return -1;
    }

    // Get status code
    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size,
                        WINHTTP_NO_HEADER_INDEX);
    resp->status_code = (int)status_code;

    // Read body into dynamically growing buffer
    size_t capacity = 4096;
    size_t body_len = 0;
    char* body = (char*)malloc(capacity);
    if (!body) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        http_set_error(resp, "Memory allocation failure");
        return -1;
    }

    DWORD bytes_available = 0;
    for (;;) {
        if (!WinHttpQueryDataAvailable(hRequest, &bytes_available)) break;
        if (bytes_available == 0) break;

        if (body_len + bytes_available + 1 > capacity) {
            while (body_len + bytes_available + 1 > capacity) capacity *= 2;
            char* new_body = (char*)realloc(body, capacity);
            if (!new_body) {
                free(body);
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                http_set_error(resp, "Memory allocation failure");
                return -1;
            }
            body = new_body;
        }

        DWORD bytes_read = 0;
        if (!WinHttpReadData(hRequest, body + body_len, bytes_available, &bytes_read)) break;
        if (bytes_read == 0) break;
        body_len += bytes_read;
    }

    body[body_len] = '\0';

    resp->body = body;
    resp->body_len = body_len;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (status_code < 200 || status_code >= 300) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "HTTP %d", (int)status_code);
        http_set_error(resp, err_buf);
        return -1;
    }

    return 0;
}

#else // POSIX

// ---------------------------------------------------------------------------
// POSIX: curl/wget fallback implementation
// ---------------------------------------------------------------------------

/// Check if a command exists on the system (by trying to run --version).
static int command_exists(const char* cmd) {
    const char* args[] = { "--version" };
    PalSpawnOpts opts = {0};
    opts.program = cmd;
    opts.args = args;
    opts.arg_count = 1;
    opts.capture_output = true;

    PalSpawnResult result = {0};
    int rc = pal_spawn(&opts, &result);
    int found = (rc == 0 && result.exit_code == 0);
    pal_spawn_result_free(&result);
    return found;
}

/// Download a file using curl. Returns 0 on success.
static int curl_download(const char* url, const char* dest_path, int* out_status) {
    const char* args[] = { "-fsSL", "--write-out", "%{http_code}", "-o", dest_path, url };
    PalSpawnOpts opts = {0};
    opts.program = "curl";
    opts.args = args;
    opts.arg_count = 6;
    opts.capture_output = true;

    PalSpawnResult result = {0};
    int rc = pal_spawn(&opts, &result);

    if (rc != 0) {
        pal_spawn_result_free(&result);
        return -1;
    }

    // Parse HTTP status from stdout (curl --write-out outputs it)
    if (out_status && result.stdout_buf) {
        *out_status = atoi(result.stdout_buf);
    }

    int exit_code = result.exit_code;
    pal_spawn_result_free(&result);
    return exit_code == 0 ? 0 : -1;
}

/// Download a file using wget. Returns 0 on success.
static int wget_download(const char* url, const char* dest_path, int* out_status) {
    const char* args[] = { "-q", "-O", dest_path, url };
    PalSpawnOpts opts = {0};
    opts.program = "wget";
    opts.args = args;
    opts.arg_count = 4;
    opts.capture_output = true;

    PalSpawnResult result = {0};
    int rc = pal_spawn(&opts, &result);

    if (rc != 0) {
        pal_spawn_result_free(&result);
        return -1;
    }

    // wget doesn't easily report HTTP status; infer from exit code
    // Exit code 8 = server error (4xx/5xx)
    if (out_status) {
        *out_status = (result.exit_code == 0) ? 200 : 0;
    }

    int exit_code = result.exit_code;
    pal_spawn_result_free(&result);
    return exit_code == 0 ? 0 : -1;
}

/// Fetch URL to memory using curl. Returns 0 on success.
static int curl_get(const char* url, HttpResponse* resp) {
    const char* args[] = { "-fsSL", url };
    PalSpawnOpts opts = {0};
    opts.program = "curl";
    opts.args = args;
    opts.arg_count = 2;
    opts.capture_output = true;

    PalSpawnResult result = {0};
    int rc = pal_spawn(&opts, &result);

    if (rc != 0) {
        http_set_error(resp, "Failed to spawn curl");
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "curl exited with code %d", result.exit_code);
        http_set_error(resp, err_buf);
        pal_spawn_result_free(&result);
        return -1;
    }

    // Transfer stdout to response body
    resp->status_code = 200;
    if (result.stdout_buf) {
        resp->body_len = strlen(result.stdout_buf);
        resp->body = result.stdout_buf;
        result.stdout_buf = NULL; // prevent free
    } else {
        resp->body = strdup("");
        resp->body_len = 0;
    }

    pal_spawn_result_free(&result);
    return 0;
}

/// Fetch URL to memory using wget. Returns 0 on success.
static int wget_get(const char* url, HttpResponse* resp) {
    const char* args[] = { "-q", "-O", "-", url };
    PalSpawnOpts opts = {0};
    opts.program = "wget";
    opts.args = args;
    opts.arg_count = 4;
    opts.capture_output = true;

    PalSpawnResult result = {0};
    int rc = pal_spawn(&opts, &result);

    if (rc != 0) {
        http_set_error(resp, "Failed to spawn wget");
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "wget exited with code %d", result.exit_code);
        http_set_error(resp, err_buf);
        pal_spawn_result_free(&result);
        return -1;
    }

    resp->status_code = 200;
    if (result.stdout_buf) {
        resp->body_len = strlen(result.stdout_buf);
        resp->body = result.stdout_buf;
        result.stdout_buf = NULL;
    } else {
        resp->body = strdup("");
        resp->body_len = 0;
    }

    pal_spawn_result_free(&result);
    return 0;
}

#endif // _WIN32 / POSIX

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int http_download(const char* url, const char* dest_path,
                  int max_retries, HttpProgressFunc progress, void* ctx) {
    if (!url || !dest_path) return -1;
    if (max_retries < 0) max_retries = 0;

    int attempts = max_retries + 1; // total attempts = 1 initial + retries
    int last_status = 0;

    for (int attempt = 0; attempt < attempts; attempt++) {
        // Exponential backoff: sleep before retry (not before first attempt)
        if (attempt > 0) {
            unsigned int delay_ms = 1000u * (1u << (unsigned)(attempt - 1)); // 1s, 2s, 4s...
            http_sleep_ms(delay_ms);
        }

        // Report start via progress callback
        if (progress) {
            progress(0, 0, ctx);
        }

        int status = 0;
        int rc = 0;

#ifdef _WIN32
        rc = winhttp_download_attempt(url, dest_path, &status, progress, ctx);
#else
        // Try curl first, then wget
        if (command_exists("curl")) {
            rc = curl_download(url, dest_path, &status);
        } else if (command_exists("wget")) {
            rc = wget_download(url, dest_path, &status);
        } else {
            fprintf(stderr, "error: neither curl nor wget found; cannot download %s\n", url);
            return -1;
        }

        // On POSIX with curl/wget, progress is limited to start/end
        if (rc == 0 && progress) {
            // Signal completion (total unknown, so pass downloaded == total)
            progress(1, 1, ctx);
        }
#endif

        last_status = status;

        if (rc == 0) {
            return 0; // success
        }
    }

    // All retries exhausted — report failure
    fprintf(stderr, "error: download failed for '%s'", url);
    if (last_status > 0) {
        fprintf(stderr, " (HTTP %d)", last_status);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "  hint: check your network connectivity and try again\n");

    return -1;
}

int http_get(const char* url, HttpResponse* resp) {
    if (!url || !resp) return -1;

    memset(resp, 0, sizeof(*resp));

    // http_get uses 3 retries (same policy as download)
    int max_retries = 3;
    int attempts = max_retries + 1;

    for (int attempt = 0; attempt < attempts; attempt++) {
        if (attempt > 0) {
            unsigned int delay_ms = 1000u * (1u << (unsigned)(attempt - 1));
            http_sleep_ms(delay_ms);

            // Free any partial response from previous attempt
            free(resp->body);
            free(resp->error_msg);
            memset(resp, 0, sizeof(*resp));
        }

        int rc = 0;

#ifdef _WIN32
        rc = winhttp_get_attempt(url, resp);
#else
        if (command_exists("curl")) {
            rc = curl_get(url, resp);
        } else if (command_exists("wget")) {
            rc = wget_get(url, resp);
        } else {
            http_set_error(resp, "neither curl nor wget found");
            return -1;
        }
#endif

        if (rc == 0) {
            return 0;
        }
    }

    // All retries exhausted
    if (!resp->error_msg) {
        char err_buf[512];
        snprintf(err_buf, sizeof(err_buf),
                 "download failed for '%s' after %d attempts (HTTP %d). "
                 "Check your network connectivity.",
                 url, attempts, resp->status_code);
        http_set_error(resp, err_buf);
    }

    return -1;
}

void http_response_free(HttpResponse* resp) {
    if (!resp) return;
    free(resp->body);
    free(resp->error_msg);
    resp->body = NULL;
    resp->error_msg = NULL;
    resp->body_len = 0;
    resp->status_code = 0;
}
