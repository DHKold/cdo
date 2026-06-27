#ifndef CDO_COMMANDS_TEST_PROTOCOL_H
#define CDO_COMMANDS_TEST_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/// Message types emitted by the JSON Lines test protocol.
typedef enum {
    TEST_MSG_SUITE_START,
    TEST_MSG_TEST_START,
    TEST_MSG_TEST_END,
    TEST_MSG_SUITE_END,
    TEST_MSG_ERROR,
    TEST_MSG_UNKNOWN
} TestMsgType;

/// Test outcome status (only meaningful for TEST_MSG_TEST_END).
typedef enum {
    TEST_STATUS_PASS,
    TEST_STATUS_FAIL,
    TEST_STATUS_SKIP
} TestStatus;

/// A parsed protocol message from a test binary's JSON Lines output.
typedef struct {
    TestMsgType msg_type;
    char        name[256];
    int         id;
    TestStatus  status;
    double      duration_ms;
    int         total;
    int         passed;
    int         failed;
    int         skipped;
    char        message[256];  // for error messages
    struct {
        char file[256];
        int  line;
        char expr[256];
        char actual[128];
        char expected[128];
    } failure;
} TestProtocolMsg;

/// Parse a single JSON Lines message from a test binary.
/// Populates `out` with the parsed fields.
/// Returns 0 on success, -1 on parse failure or malformed input.
int test_protocol_parse_line(const char *line, TestProtocolMsg *out);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_TEST_PROTOCOL_H
