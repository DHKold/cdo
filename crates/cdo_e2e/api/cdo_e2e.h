#ifndef CDO_E2E_H
#define CDO_E2E_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdo_ut.h"
#include "pal/pal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Error Codes
// ============================================================================

#define E2E_OK           0
#define E2E_ERR_IO       1
#define E2E_ERR_TIMEOUT  2
#define E2E_ERR_INVALID  3
#define E2E_ERR_NOT_FOUND 4
#define E2E_ERR_LIMIT    5
#define E2E_ERR_SPAWN    6

// ============================================================================
// Constants
// ============================================================================

#define E2E_ENV_MAX_VARS 64
#define E2E_ENV_MAX_ARGS 128

// ============================================================================
// Data Types
// ============================================================================

/// A key-value pair for environment variable overrides.
typedef struct {
    char key[128];
    char value[512];
} E2eEnvVar;

/// Isolated test environment for a single e2e test.
typedef struct E2eEnv {
    char root_path[260];                 // Absolute path to temp directory
    char crate_path[260];                // Crate root (for fixture resolution)
    E2eEnvVar env_vars[E2E_ENV_MAX_VARS]; // Environment variable overrides
    int env_var_count;                   // Number of active overrides
    bool keep_temps;                     // If true, don't delete on cleanup
} E2eEnv;

/// Result of spawning a subprocess within an E2E test environment.
typedef struct E2eSpawnResult {
    int   exit_code;           // Process exit code
    char* stdout_buf;          // Captured stdout (heap-allocated, up to 16 MB)
    char* stderr_buf;          // Captured stderr (heap-allocated, up to 16 MB)
    bool  timed_out;           // True if process was terminated due to timeout
    char  error_desc[256];     // Error description if spawn failed
} E2eSpawnResult;

/// Options for spawning a subprocess in an E2E test environment.
typedef struct E2eSpawnOpts {
    const char* executable;              // Path to executable
    const char* args[E2E_ENV_MAX_ARGS];  // Argument list
    int         arg_count;               // Number of arguments
    const char* working_dir;             // Override working directory (NULL = env root)
    int         timeout_ms;              // Timeout in ms (0 = use default 120000)
    E2eEnvVar*  extra_env;               // Additional env vars merged with E2eEnv's
    int         extra_env_count;         // Number of extra env vars
} E2eSpawnOpts;

// ============================================================================
// Test Environment Setup (Requirement 4)
// ============================================================================

/// Create an isolated temporary directory for the test.
/// The directory name includes a sanitized test_name (up to 64 chars) and a unique suffix.
/// @param test_name  Human-readable name for the test (used in dir name)
/// @param env_out    Pointer to E2eEnv struct to initialize
/// @return 0 on success, non-zero error code on failure
int e2e_env_create(const char* test_name, E2eEnv* env_out);

/// Create a file within the test environment with given content.
/// Creates intermediate directories as needed.
/// Rejects paths that escape the environment root (e.g., containing ".." that resolves outside).
/// @param env         Active test environment
/// @param rel_path    Relative path within the environment
/// @param content     File content data
/// @param content_len Content length in bytes
/// @return 0 on success, non-zero error code on failure
int e2e_env_write_file(E2eEnv* env, const char* rel_path, const void* content, size_t content_len);

/// Create a directory within the test environment.
/// Creates intermediate directories as needed.
/// Rejects paths that escape the environment root.
/// @param env       Active test environment
/// @param rel_path  Relative path for the directory
/// @return 0 on success, non-zero error code on failure
int e2e_env_mkdir(E2eEnv* env, const char* rel_path);

/// Set an environment variable override for subprocesses spawned in this environment.
/// Does NOT modify the host process environment.
/// @param env    Active test environment
/// @param key    Variable name
/// @param value  Variable value
/// @return 0 on success, non-zero if limit reached
int e2e_env_setvar(E2eEnv* env, const char* key, const char* value);

/// Destroy the test environment: delete the temp directory and all contents recursively.
/// If env->keep_temps is true, logs the path and skips deletion.
/// @param env  Test environment to destroy
/// @return 0 on success, non-zero on failure (logged as warning, non-fatal)
int e2e_env_destroy(E2eEnv* env);

/// Set the crate root path used for fixture resolution.
/// Must be called before any e2e_fixture_copy operations.
/// @param env         Active test environment
/// @param crate_path  Absolute path to the crate root directory
/// @return 0 on success, non-zero if path is NULL or too long
int e2e_env_set_crate_path(E2eEnv* env, const char* crate_path);

// ============================================================================
// Subprocess Execution (Requirement 5)
// ============================================================================

/// Spawn a subprocess within the test environment context.
/// Merges environment variables from E2eEnv + opts->extra_env.
/// Captures stdout/stderr up to 16 MB each.
/// Applies timeout (default 120000ms if opts->timeout_ms == 0).
/// @param env     Active test environment
/// @param opts    Spawn configuration
/// @param result  Output spawn result (caller must free with e2e_spawn_result_free)
/// @return 0 on success (process ran, check result->exit_code), non-zero on spawn failure
int e2e_spawn(E2eEnv* env, const E2eSpawnOpts* opts, E2eSpawnResult* result);

/// Free resources associated with a spawn result (stdout_buf, stderr_buf).
/// @param result  Spawn result to free
void e2e_spawn_result_free(E2eSpawnResult* result);

// ============================================================================
// Fixture Management (Requirement 7)
// ============================================================================

/// Recursively copy a named fixture into the test environment.
/// Fixture is located at <crate_path>/e2e/fixtures/<fixture_name>/.
/// Reproduces all subdirectories and files with identical relative paths.
/// @param env           Active test environment (must have crate_path set)
/// @param fixture_name  Name of the fixture (alphanumeric, hyphens, underscores; max 64 chars)
/// @return 0 on success, non-zero on failure (missing fixture, copy error)
int e2e_fixture_copy(E2eEnv* env, const char* fixture_name);

// ============================================================================
// Assertions (Requirement 6)
// ============================================================================

/// Assert that spawn result exit code equals expected.
/// On failure: records via cdo_ut_record_failure and causes calling function to return 1.
#define E2E_ASSERT_EXIT_CODE(result, expected_code) \
    do { if ((result)->exit_code != (expected_code)) { \
        char _actual[32], _expected[32]; \
        snprintf(_actual, sizeof(_actual), "%d", (result)->exit_code); \
        snprintf(_expected, sizeof(_expected), "%d", (expected_code)); \
        cdo_ut_record_failure(__FILE__, __LINE__, "exit_code == " #expected_code, _actual, _expected); \
        return 1; \
    } } while(0)

/// Assert that spawn result stdout contains a substring (case-sensitive).
#define E2E_ASSERT_STDOUT_CONTAINS(result, substring) \
    do { \
        if ((result)->stdout_buf == NULL) { \
            cdo_ut_record_failure(__FILE__, __LINE__, "stdout contains " #substring, "(stdout not captured)", #substring); \
            return 1; \
        } \
        if (strstr((result)->stdout_buf, (substring)) == NULL) { \
            cdo_ut_record_failure(__FILE__, __LINE__, "stdout contains " #substring, (result)->stdout_buf, (substring)); \
            return 1; \
        } \
    } while(0)

/// Assert that spawn result stderr contains a substring (case-sensitive).
#define E2E_ASSERT_STDERR_CONTAINS(result, substring) \
    do { \
        if ((result)->stderr_buf == NULL) { \
            cdo_ut_record_failure(__FILE__, __LINE__, "stderr contains " #substring, "(stderr not captured)", #substring); \
            return 1; \
        } \
        if (strstr((result)->stderr_buf, (substring)) == NULL) { \
            cdo_ut_record_failure(__FILE__, __LINE__, "stderr contains " #substring, (result)->stderr_buf, (substring)); \
            return 1; \
        } \
    } while(0)

/// Assert that a file exists at the given absolute path.
/// Uses PAL convention: pal_path_exists returns 0 on success (path exists).
#define E2E_ASSERT_FILE_EXISTS(path) \
    do { if (pal_path_exists(path) != 0) { \
        cdo_ut_record_failure(__FILE__, __LINE__, "file exists: " #path, "(not found)", (path)); \
        return 1; \
    } } while(0)

/// Assert that a file does NOT exist at the given absolute path.
/// Uses PAL convention: pal_path_exists returns 0 on success (path exists).
#define E2E_ASSERT_FILE_NOT_EXISTS(path) \
    do { if (pal_path_exists(path) == 0) { \
        cdo_ut_record_failure(__FILE__, __LINE__, "file not exists: " #path, "(exists)", (path)); \
        return 1; \
    } } while(0)

/// Assert that a file's content contains a substring (case-sensitive).
/// Reads the file, searches for substring. Reports path on failure.
#define E2E_ASSERT_FILE_CONTAINS(filepath, substring) \
    do { \
        char* _fc_buf = NULL; size_t _fc_len = 0; \
        if (pal_file_read((filepath), &_fc_buf, &_fc_len) != 0) { \
            cdo_ut_record_failure(__FILE__, __LINE__, "file readable: " #filepath, "(read failed)", (filepath)); \
            return 1; \
        } \
        bool _fc_found = (strstr(_fc_buf, (substring)) != NULL); \
        free(_fc_buf); \
        if (!_fc_found) { \
            cdo_ut_record_failure(__FILE__, __LINE__, "file contains " #substring, (filepath), (substring)); \
            return 1; \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif // CDO_E2E_H
