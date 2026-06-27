#ifndef CDO_HTTP_H
#define CDO_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int         status_code;
    char*       body;
    size_t      body_len;
    char*       error_msg;      // NULL on success
} HttpResponse;

typedef void (*HttpProgressFunc)(size_t downloaded, size_t total, void* ctx);

// Download a URL to a file. Retries up to max_retries with exponential backoff.
// Returns 0 on success, non-zero on failure.
int http_download(const char* url, const char* dest_path,
                  int max_retries, HttpProgressFunc progress, void* ctx);

// Fetch a URL into memory (for small payloads like registry metadata).
// Returns 0 on success, non-zero on failure.
int http_get(const char* url, HttpResponse* resp);

// Free resources allocated in an HttpResponse.
void http_response_free(HttpResponse* resp);

#ifdef __cplusplus
}
#endif

#endif // CDO_HTTP_H
