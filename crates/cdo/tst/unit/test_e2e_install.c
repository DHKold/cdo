// crates/cdo/tst/unit/test_e2e_install.c
// End-to-end tests for the install and uninstall flow.
// Spawns `cdo.exe` as a subprocess against the e2e/install_basic workspace.
// All tests use --path <tmpdir> to avoid polluting the real ~/.cdo/.
// Validates: Requirements REQ-INSTALL-1, REQ-INSTALL-3, REQ-INSTALL-8, REQ-INSTALL-9
#include "cdo_ut.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define get_cwd _getcwd
#else
#include <unistd.h>
#define get_cwd getcwd
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

typedef struct {
    char cdo_exe[512];        // Path to cdo.exe (project root)
    char e2e_workspace[512];  // Path to e2e/install_basic/
    char install_dir[512];    // Temp install path (--path target)
} E2eInstallFixture;

/// Get the project root from CWD (tests always run with CWD = project root).
static int get_project_root(char* root, size_t root_size) {
    if (get_cwd(root, (int)root_size) == NULL) return 1;
    pal_path_normalize(root);
    return 0;
}

static int e2e_fixture_init(E2eInstallFixture* f, const char* suffix) {
    char project_root[512];
    if (get_project_root(project_root, sizeof(project_root)) != 0) return 1;

    // The built cdo.exe (with install command) is at build/debug/cdo/cdo.exe.
    // We must copy it to a temp location (cdo_temp.exe in project root) to avoid
    // file-locking issues when the binary tries to rebuild itself.
    char built_exe[512];
    if (pal_path_join(built_exe, sizeof(built_exe), project_root, "build/debug/cdo/cdo.exe") != 0) return 1;
    pal_path_normalize(built_exe);

    if (pal_path_exists(built_exe) != 0) return 1;

    // Copy to cdo_temp.exe in the project root
    if (pal_path_join(f->cdo_exe, sizeof(f->cdo_exe), project_root, "cdo_temp.exe") != 0) return 1;
    pal_path_normalize(f->cdo_exe);
    if (pal_file_copy(built_exe, f->cdo_exe) != 0) return 1;

    // e2e workspace
    if (pal_path_join(f->e2e_workspace, sizeof(f->e2e_workspace), project_root, "e2e/install_basic") != 0) return 1;
    pal_path_normalize(f->e2e_workspace);

    // Create temp install dir
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    snprintf(f->install_dir, sizeof(f->install_dir), "%s/cdo_e2e_install_%s", tmp, suffix);
    pal_path_normalize(f->install_dir);

    // Clean any prior run
    pal_rmdir_r(f->install_dir);
    return 0;
}

static void e2e_fixture_destroy(E2eInstallFixture* f) {
    pal_rmdir_r(f->install_dir);
    // Note: cdo_temp.exe is left in place for subsequent tests in the same run
}

/// Run cdo.exe with the given raw command line (appended after the exe path).
/// Returns the process exit code, or -1 on spawn failure.
static int run_cdo(E2eInstallFixture* f, const char* args_str, char* stdout_buf, size_t stdout_size) {
    // Build the full command line: "cdo.exe <args>"
    char cmdline[2048];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", f->cdo_exe, args_str);

    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = f->cdo_exe;
    opts.raw_cmdline = cmdline;
    opts.cwd = f->e2e_workspace;
    opts.capture_output = true;
    opts.timeout_ms = 60000; // 60 seconds for build + install

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));
    int rc = pal_spawn(&opts, &result);
    if (rc != 0) {
        pal_spawn_result_free(&result);
        return -1;
    }

    // Copy stdout if requested
    if (stdout_buf && stdout_size > 0 && result.stdout_buf) {
        size_t len = strlen(result.stdout_buf);
        size_t copy_len = len < stdout_size - 1 ? len : stdout_size - 1;
        memcpy(stdout_buf, result.stdout_buf, copy_len);
        stdout_buf[copy_len] = '\0';
    } else if (stdout_buf && stdout_size > 0) {
        stdout_buf[0] = '\0';
    }

    int exit_code = result.exit_code;
    pal_spawn_result_free(&result);
    return exit_code;
}

/// Check if a path exists (file or directory).
static bool e2e_path_exists(const char* path) {
    return pal_path_exists(path) == 0;
}

/// Check if a file exists at base/rel.
static bool e2e_file_exists(const char* base, const char* rel) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), base, rel) != 0) return false;
    pal_path_normalize(full);
    return pal_path_exists(full) == 0;
}

/// Read file content from base/rel into buf.
static int e2e_read_file(const char* base, const char* rel, char* buf, size_t buf_size) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), base, rel) != 0) return 1;
    pal_path_normalize(full);
    char* data = NULL;
    size_t len = 0;
    int rc = pal_file_read(full, &data, &len);
    if (rc != 0) return 1;
    size_t copy_len = len < buf_size - 1 ? len : buf_size - 1;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';
    free(data);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: Full install flow — run `cdo install --path <tmpdir>`, verify artifacts
// Validates: REQ-INSTALL-1, REQ-INSTALL-3
// ---------------------------------------------------------------------------

TEST_SERIAL(e2e_install_full_flow) {
    E2eInstallFixture f;
    if (e2e_fixture_init(&f, "full_flow") != 0) {
        fprintf(stderr, "e2e_install_full_flow: fixture init failed (cdo.exe not found?)\n");
        return 1;
    }

    // Run: cdo install --path <tmpdir>
    char args[1024];
    snprintf(args, sizeof(args), "install --path \"%s\"", f.install_dir);

    char stdout_buf[4096] = {0};
    int exit_code = run_cdo(&f, args, stdout_buf, sizeof(stdout_buf));
    if (exit_code != 0) {
        fprintf(stderr, "e2e_install_full_flow: cdo install returned %d\nstdout: %s\n", exit_code, stdout_buf);
    }
    TEST_ASSERT_EQ(exit_code, 0);

    // Verify app directory created: <install_dir>/apps/hello/
    TEST_ASSERT(e2e_file_exists(f.install_dir, "apps/hello"));

    // Verify executable exists in app dir
    TEST_ASSERT(e2e_file_exists(f.install_dir, "apps/hello/hello.exe"));

    // Verify manifest written
    TEST_ASSERT(e2e_file_exists(f.install_dir, "apps/hello/manifest.toml"));

    // Verify manifest contains expected fields
    char manifest_buf[4096] = {0};
    TEST_ASSERT_EQ(e2e_read_file(f.install_dir, "apps/hello/manifest.toml", manifest_buf, sizeof(manifest_buf)), 0);
    TEST_ASSERT(strstr(manifest_buf, "name = \"hello\"") != NULL);
    TEST_ASSERT(strstr(manifest_buf, "version = \"1.0.0\"") != NULL);

    // Verify launcher exists in bin/
    TEST_ASSERT(e2e_file_exists(f.install_dir, "bin/hello.cmd"));

    // Verify launcher content
    char launcher_buf[512] = {0};
    TEST_ASSERT_EQ(e2e_read_file(f.install_dir, "bin/hello.cmd", launcher_buf, sizeof(launcher_buf)), 0);
    TEST_ASSERT(strstr(launcher_buf, "apps\\hello\\hello.exe") != NULL);

    // Verify global index updated
    TEST_ASSERT(e2e_file_exists(f.install_dir, "apps/install.toml"));
    char index_buf[4096] = {0};
    TEST_ASSERT_EQ(e2e_read_file(f.install_dir, "apps/install.toml", index_buf, sizeof(index_buf)), 0);
    TEST_ASSERT(strstr(index_buf, "name = \"hello\"") != NULL);
    TEST_ASSERT(strstr(index_buf, "version = \"1.0.0\"") != NULL);

    e2e_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: `cdo install --list` shows the installed app
// Validates: REQ-INSTALL-9
// ---------------------------------------------------------------------------

TEST_SERIAL(e2e_install_list_shows_installed_app) {
    E2eInstallFixture f;
    if (e2e_fixture_init(&f, "list_show") != 0) {
        fprintf(stderr, "e2e_install_list_shows_installed_app: fixture init failed\n");
        return 1;
    }

    // First, install the app
    char install_args[1024];
    snprintf(install_args, sizeof(install_args), "install --path \"%s\"", f.install_dir);
    int rc = run_cdo(&f, install_args, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    // Now run --list
    char list_args[1024];
    snprintf(list_args, sizeof(list_args), "install --list --path \"%s\"", f.install_dir);
    char stdout_buf[4096] = {0};
    rc = run_cdo(&f, list_args, stdout_buf, sizeof(stdout_buf));
    TEST_ASSERT_EQ(rc, 0);

    // Output should contain the app name and version
    TEST_ASSERT(strstr(stdout_buf, "hello") != NULL);
    TEST_ASSERT(strstr(stdout_buf, "1.0.0") != NULL);

    e2e_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: Reinstall from same workspace succeeds without --force
// Validates: REQ-INSTALL-1 (reinstall behavior)
// ---------------------------------------------------------------------------

TEST_SERIAL(e2e_install_reinstall_same_workspace_succeeds) {
    E2eInstallFixture f;
    if (e2e_fixture_init(&f, "reinstall") != 0) {
        fprintf(stderr, "e2e_install_reinstall_same_workspace_succeeds: fixture init failed\n");
        return 1;
    }

    // Install the first time
    char args[1024];
    snprintf(args, sizeof(args), "install --path \"%s\"", f.install_dir);
    int rc = run_cdo(&f, args, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    // Verify it was installed
    TEST_ASSERT(e2e_file_exists(f.install_dir, "apps/hello/hello.exe"));

    // Reinstall from the same workspace — should succeed without --force
    char stdout_buf[4096] = {0};
    rc = run_cdo(&f, args, stdout_buf, sizeof(stdout_buf));
    if (rc != 0) {
        fprintf(stderr, "e2e_install_reinstall: second install returned %d\nstdout: %s\n", rc, stdout_buf);
    }
    TEST_ASSERT_EQ(rc, 0);

    // Verify app is still installed properly
    TEST_ASSERT(e2e_file_exists(f.install_dir, "apps/hello/hello.exe"));
    TEST_ASSERT(e2e_file_exists(f.install_dir, "apps/hello/manifest.toml"));
    TEST_ASSERT(e2e_file_exists(f.install_dir, "bin/hello.cmd"));

    e2e_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: `cdo uninstall hello` removes app dir, launcher, and index entry
// Validates: REQ-INSTALL-8
// ---------------------------------------------------------------------------

TEST_SERIAL(e2e_uninstall_removes_all_artifacts) {
    E2eInstallFixture f;
    if (e2e_fixture_init(&f, "uninstall") != 0) {
        fprintf(stderr, "e2e_uninstall_removes_all_artifacts: fixture init failed\n");
        return 1;
    }

    // First install
    char install_args[1024];
    snprintf(install_args, sizeof(install_args), "install --path \"%s\"", f.install_dir);
    int rc = run_cdo(&f, install_args, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    // Verify installed
    TEST_ASSERT(e2e_file_exists(f.install_dir, "apps/hello/hello.exe"));
    TEST_ASSERT(e2e_file_exists(f.install_dir, "bin/hello.cmd"));
    TEST_ASSERT(e2e_file_exists(f.install_dir, "apps/install.toml"));

    // Uninstall
    char uninstall_args[1024];
    snprintf(uninstall_args, sizeof(uninstall_args), "uninstall hello --path \"%s\"", f.install_dir);
    char stdout_buf[4096] = {0};
    rc = run_cdo(&f, uninstall_args, stdout_buf, sizeof(stdout_buf));
    if (rc != 0) {
        fprintf(stderr, "e2e_uninstall: returned %d\nstdout: %s\n", rc, stdout_buf);
    }
    TEST_ASSERT_EQ(rc, 0);

    // Verify app directory removed
    TEST_ASSERT(!e2e_file_exists(f.install_dir, "apps/hello/hello.exe"));
    TEST_ASSERT(!e2e_file_exists(f.install_dir, "apps/hello/manifest.toml"));
    TEST_ASSERT(!e2e_file_exists(f.install_dir, "apps/hello"));

    // Verify launcher removed
    TEST_ASSERT(!e2e_file_exists(f.install_dir, "bin/hello.cmd"));

    // Verify global index no longer contains the entry
    char index_buf[4096] = {0};
    if (e2e_read_file(f.install_dir, "apps/install.toml", index_buf, sizeof(index_buf)) == 0) {
        // If the file still exists, it should not contain "hello"
        TEST_ASSERT(strstr(index_buf, "name = \"hello\"") == NULL);
    }
    // If the file doesn't exist at all, that's also acceptable

    e2e_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: `cdo install --path <tmpdir>` installs to custom directory
// Validates: REQ-INSTALL-3
// ---------------------------------------------------------------------------

TEST_SERIAL(e2e_install_custom_path) {
    E2eInstallFixture f;
    if (e2e_fixture_init(&f, "custom_path") != 0) {
        fprintf(stderr, "e2e_install_custom_path: fixture init failed\n");
        return 1;
    }

    // Use a custom subdirectory within the temp dir
    char custom_path[512];
    snprintf(custom_path, sizeof(custom_path), "%s/custom_install_dir", f.install_dir);
    pal_path_normalize(custom_path);

    // Verify the custom path does NOT exist yet
    TEST_ASSERT(!e2e_path_exists(custom_path));

    // Install to the custom path
    char args[1024];
    snprintf(args, sizeof(args), "install --path \"%s\"", custom_path);
    char stdout_buf[4096] = {0};
    int rc = run_cdo(&f, args, stdout_buf, sizeof(stdout_buf));
    if (rc != 0) {
        fprintf(stderr, "e2e_install_custom_path: cdo install returned %d\nstdout: %s\n", rc, stdout_buf);
    }
    TEST_ASSERT_EQ(rc, 0);

    // Verify installation in the custom path
    TEST_ASSERT(e2e_file_exists(custom_path, "apps/hello/hello.exe"));
    TEST_ASSERT(e2e_file_exists(custom_path, "apps/hello/manifest.toml"));
    TEST_ASSERT(e2e_file_exists(custom_path, "bin/hello.cmd"));
    TEST_ASSERT(e2e_file_exists(custom_path, "apps/install.toml"));

    // Verify manifest content at custom path
    char manifest_buf[4096] = {0};
    TEST_ASSERT_EQ(e2e_read_file(custom_path, "apps/hello/manifest.toml", manifest_buf, sizeof(manifest_buf)), 0);
    TEST_ASSERT(strstr(manifest_buf, "name = \"hello\"") != NULL);
    TEST_ASSERT(strstr(manifest_buf, "version = \"1.0.0\"") != NULL);

    // Cleanup the custom path (e2e_fixture_destroy only cleans f.install_dir)
    pal_rmdir_r(custom_path);
    e2e_fixture_destroy(&f);
    return 0;
}
