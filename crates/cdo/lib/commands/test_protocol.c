#include "commands/test_protocol.h"
#include "core/json.h"
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Safe string copy into a fixed buffer with guaranteed null-termination.
static void safe_strncpy(char *dst, size_t dst_size, const char *src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/// Extract a string field from a JSON object into a fixed buffer.
/// Does nothing if the key is missing or not a string.
static void extract_string(const JsonValue *obj, const char *key,
                           char *buf, size_t buf_size) {
    const JsonValue *val = json_get(obj, key);
    if (val && json_type(val) == JSON_STRING) {
        safe_strncpy(buf, buf_size, json_string(val));
    }
}

/// Extract an integer (stored as JSON number) from a JSON object.
/// Returns 0 if the key is missing or not a number.
static int extract_int(const JsonValue *obj, const char *key) {
    const JsonValue *val = json_get(obj, key);
    if (val && json_type(val) == JSON_NUMBER) {
        return (int)json_number(val);
    }
    return 0;
}

/// Extract a double from a JSON object.
/// Returns 0.0 if the key is missing or not a number.
static double extract_double(const JsonValue *obj, const char *key) {
    const JsonValue *val = json_get(obj, key);
    if (val && json_type(val) == JSON_NUMBER) {
        return json_number(val);
    }
    return 0.0;
}

/// Map a status string to a TestStatus enum value.
static TestStatus parse_status(const char *s) {
    if (!s) return TEST_STATUS_PASS;
    if (strcmp(s, "fail") == 0) return TEST_STATUS_FAIL;
    if (strcmp(s, "skip") == 0) return TEST_STATUS_SKIP;
    return TEST_STATUS_PASS;
}

/// Map a type string to a TestMsgType enum value.
static TestMsgType parse_msg_type(const char *s) {
    if (!s) return TEST_MSG_UNKNOWN;
    if (strcmp(s, "suite_start") == 0) return TEST_MSG_SUITE_START;
    if (strcmp(s, "test_start") == 0)  return TEST_MSG_TEST_START;
    if (strcmp(s, "test_end") == 0)    return TEST_MSG_TEST_END;
    if (strcmp(s, "suite_end") == 0)   return TEST_MSG_SUITE_END;
    if (strcmp(s, "error") == 0)       return TEST_MSG_ERROR;
    return TEST_MSG_UNKNOWN;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int test_protocol_parse_line(const char *line, TestProtocolMsg *out) {
    if (!line || !out) return -1;

    // Zero-initialize the output struct
    memset(out, 0, sizeof(*out));
    out->msg_type = TEST_MSG_UNKNOWN;

    // Parse the JSON line
    JsonValue *root = NULL;
    JsonError err = {0};
    size_t len = strlen(line);

    if (json_parse(line, len, &root, &err) != 0) {
        return -1;
    }

    // The root must be an object
    if (json_type(root) != JSON_OBJECT) {
        json_free(root);
        return -1;
    }

    // Extract the "type" field (required)
    const JsonValue *type_val = json_get(root, "type");
    if (!type_val || json_type(type_val) != JSON_STRING) {
        json_free(root);
        return -1;
    }

    const char *type_str = json_string(type_val);
    out->msg_type = parse_msg_type(type_str);

    if (out->msg_type == TEST_MSG_UNKNOWN) {
        json_free(root);
        return -1;
    }

    // Extract fields based on message type
    switch (out->msg_type) {
    case TEST_MSG_SUITE_START:
        out->total = extract_int(root, "total");
        break;

    case TEST_MSG_TEST_START:
        extract_string(root, "name", out->name, sizeof(out->name));
        out->id = extract_int(root, "id");
        break;

    case TEST_MSG_TEST_END: {
        extract_string(root, "name", out->name, sizeof(out->name));
        out->id = extract_int(root, "id");
        out->duration_ms = extract_double(root, "duration_ms");

        // Parse status
        const JsonValue *status_val = json_get(root, "status");
        if (status_val && json_type(status_val) == JSON_STRING) {
            out->status = parse_status(json_string(status_val));
        }

        // Parse optional failure object
        const JsonValue *failure_val = json_get(root, "failure");
        if (failure_val && json_type(failure_val) == JSON_OBJECT) {
            extract_string(failure_val, "file", out->failure.file,
                           sizeof(out->failure.file));
            out->failure.line = extract_int(failure_val, "line");
            extract_string(failure_val, "expr", out->failure.expr,
                           sizeof(out->failure.expr));
            extract_string(failure_val, "actual", out->failure.actual,
                           sizeof(out->failure.actual));
            extract_string(failure_val, "expected", out->failure.expected,
                           sizeof(out->failure.expected));
        }
        break;
    }

    case TEST_MSG_SUITE_END:
        out->total = extract_int(root, "total");
        out->passed = extract_int(root, "passed");
        out->failed = extract_int(root, "failed");
        out->skipped = extract_int(root, "skipped");
        out->duration_ms = extract_double(root, "duration_ms");
        break;

    case TEST_MSG_ERROR:
        extract_string(root, "message", out->message, sizeof(out->message));
        break;

    default:
        break;
    }

    json_free(root);
    return 0;
}
