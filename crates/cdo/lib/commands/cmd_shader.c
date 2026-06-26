#include "commands/cmd_shader.h"
#include "core/shader.h"
#include "core/output.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// cmd_shader implementation
// ---------------------------------------------------------------------------

int cmd_shader(const CdoOptions* opts) {
    if (!opts) {
        cdo_error("internal error: NULL options passed to shader command");
        return 1;
    }

    if (opts->help) {
        cdo_cli_print_help(CDO_CMD_SHADER, stdout);
        return 0;
    }

    // --- Step 1: Determine DXC path ---
    char dxc_path[512];
#ifdef _WIN32
    pal_path_join(dxc_path, sizeof(dxc_path), ".cdo/tools/dxc/bin", "dxc.exe");
#else
    pal_path_join(dxc_path, sizeof(dxc_path), ".cdo/tools/dxc/bin", "dxc");
#endif

    // --- Step 2: Verify DXC is installed ---
    if (pal_path_exists(dxc_path) != 1) {
        cdo_error("DXC shader compiler not found at: %s", dxc_path);
        cdo_info("Try: cdo tool install dxc");
        return 1;
    }

    // --- Step 3: Determine shader directory ---
    const char* shader_dir = "shaders/";
    if (opts->positional_count > 0 && opts->positional_args[0]) {
        shader_dir = opts->positional_args[0];
    }

    // --- Step 4: Determine output directory ---
    const char* output_dir = "build/shaders/";
    if (opts->positional_count > 1 && opts->positional_args[1]) {
        output_dir = opts->positional_args[1];
    }

    // --- Step 5: Compile shaders ---
    cdo_info("compiling shaders: %s -> %s", shader_dir, output_dir);

    int compiled_count = 0;
    int skipped_count = 0;
    int rc = shader_compile(shader_dir, output_dir, dxc_path,
                            &compiled_count, &skipped_count);

    if (rc != 0) {
        cdo_error("shader compilation failed");
        return 1;
    }

    // --- Step 6: Report results ---
    cdo_info("shaders: %d compiled, %d up-to-date", compiled_count, skipped_count);
    return 0;
}
