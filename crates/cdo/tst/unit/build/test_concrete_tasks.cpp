// test_concrete_tasks.cpp — Unit tests for concrete task construction and config.
// Validates: Requirements 3.3, 3.4, 3.5, 3.6, 3.7, 3.8
//
// Tests that each concrete task type:
//   - Constructs without crash
//   - inputs() returns the expected set of artifacts
//   - outputs() returns the expected artifacts
//   - primaryOutput() returns the correct primary artifact
//   - condition() returns a valid FreshnessCondition reference

#include "cdo_ut.h"
#include "build/tasks/build_c_source.h"
#include "build/tasks/build_cpp_source.h"
#include "build/tasks/build_static_library.h"
#include "build/tasks/build_executable.h"
#include "build/tasks/build_shared_library.h"
#include "build/tasks/compile_hlsl_shader.h"

using namespace cdo::build;

// =============================================================================
// BuildCSource Tests
// =============================================================================

TEST(build_c_source_construction_no_headers) {
    BuildCSource::Config cfg;
    cfg.source_path = "src/main.c";
    cfg.object_path = "build/debug/main.o";
    cfg.depfile_path = "build/debug/main.d";
    cfg.compiler_path = "/usr/bin/gcc";

    BuildCSource task(cfg);

    // inputs: source only (no headers)
    TEST_ASSERT_EQ((int)task.inputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "src/main.c");

    // outputs: .o + .d
    TEST_ASSERT_EQ((int)task.outputs().size(), 2);
    TEST_ASSERT_STR_EQ(task.outputs()[0]->path().c_str(), "build/debug/main.o");
    TEST_ASSERT_STR_EQ(task.outputs()[1]->path().c_str(), "build/debug/main.d");

    // primaryOutput is .o
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/main.o");

    return 0;
}

TEST(build_c_source_construction_with_headers) {
    BuildCSource::Config cfg;
    cfg.source_path = "src/util.c";
    cfg.object_path = "build/debug/util.o";
    cfg.depfile_path = "build/debug/util.d";
    cfg.compiler_path = "/usr/bin/gcc";
    cfg.header_deps = { "include/util.h", "include/common.h", "include/types.h" };

    BuildCSource task(cfg);

    // inputs: source + 3 headers = 4 total
    TEST_ASSERT_EQ((int)task.inputs().size(), 4);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "src/util.c");
    TEST_ASSERT_STR_EQ(task.inputs()[1]->path().c_str(), "include/util.h");
    TEST_ASSERT_STR_EQ(task.inputs()[2]->path().c_str(), "include/common.h");
    TEST_ASSERT_STR_EQ(task.inputs()[3]->path().c_str(), "include/types.h");

    // outputs: .o + .d
    TEST_ASSERT_EQ((int)task.outputs().size(), 2);

    // primaryOutput is still .o
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/util.o");

    return 0;
}

TEST(build_c_source_condition_is_valid) {
    BuildCSource::Config cfg;
    cfg.source_path = "src/main.c";
    cfg.object_path = "build/debug/main.o";
    cfg.depfile_path = "build/debug/main.d";

    BuildCSource task(cfg);

    // condition() should return a reference to a valid TaskCondition
    // Verify it can evaluate without crashing (output doesn't exist → Build)
    const TaskCondition& cond = task.condition();
    ConditionResult result = cond.evaluate(task.inputs(), task.primaryOutput());
    TEST_ASSERT_EQ((int)result.decision, (int)ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "does not exist");

    return 0;
}

TEST(build_c_source_forced_condition) {
    BuildCSource::Config cfg;
    cfg.source_path = "src/main.c";
    cfg.object_path = "build/debug/main.o";
    cfg.depfile_path = "build/debug/main.d";

    BuildCSource task(cfg, true); // forced=true

    // When forced and output doesn't exist, it should still say "does not exist"
    // (forced only matters when output IS up-to-date)
    const TaskCondition& cond = task.condition();
    ConditionResult result = cond.evaluate(task.inputs(), task.primaryOutput());
    TEST_ASSERT_EQ((int)result.decision, (int)ConditionResult::Build);

    return 0;
}

// =============================================================================
// BuildCppSource Tests
// =============================================================================

TEST(build_cpp_source_construction_no_headers) {
    BuildCppSource::Config cfg;
    cfg.source_path = "src/engine.cpp";
    cfg.object_path = "build/debug/engine.o";
    cfg.depfile_path = "build/debug/engine.d";
    cfg.cpp_standard = "c++17";
    cfg.compiler_path = "/usr/bin/g++";

    BuildCppSource task(cfg);

    // inputs: source only
    TEST_ASSERT_EQ((int)task.inputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "src/engine.cpp");

    // outputs: .o + .d
    TEST_ASSERT_EQ((int)task.outputs().size(), 2);
    TEST_ASSERT_STR_EQ(task.outputs()[0]->path().c_str(), "build/debug/engine.o");
    TEST_ASSERT_STR_EQ(task.outputs()[1]->path().c_str(), "build/debug/engine.d");

    // primaryOutput is .o
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/engine.o");

    return 0;
}

TEST(build_cpp_source_construction_with_headers) {
    BuildCppSource::Config cfg;
    cfg.source_path = "src/renderer.cpp";
    cfg.object_path = "build/release/renderer.o";
    cfg.depfile_path = "build/release/renderer.d";
    cfg.cpp_standard = "c++17";
    cfg.compiler_path = "/usr/bin/g++";
    cfg.header_deps = { "include/renderer.h", "include/math.h" };
    cfg.optimize = true;

    BuildCppSource task(cfg);

    // inputs: source + 2 headers = 3
    TEST_ASSERT_EQ((int)task.inputs().size(), 3);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "src/renderer.cpp");
    TEST_ASSERT_STR_EQ(task.inputs()[1]->path().c_str(), "include/renderer.h");
    TEST_ASSERT_STR_EQ(task.inputs()[2]->path().c_str(), "include/math.h");

    // outputs: .o + .d
    TEST_ASSERT_EQ((int)task.outputs().size(), 2);
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/release/renderer.o");

    return 0;
}

TEST(build_cpp_source_condition_is_valid) {
    BuildCppSource::Config cfg;
    cfg.source_path = "src/main.cpp";
    cfg.object_path = "build/debug/main.o";
    cfg.depfile_path = "build/debug/main.d";

    BuildCppSource task(cfg);

    const TaskCondition& cond = task.condition();
    ConditionResult result = cond.evaluate(task.inputs(), task.primaryOutput());
    TEST_ASSERT_EQ((int)result.decision, (int)ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "does not exist");

    return 0;
}

// =============================================================================
// BuildStaticLibrary Tests
// =============================================================================

TEST(build_static_library_construction) {
    BuildStaticLibrary::Config cfg;
    cfg.object_paths = { "build/debug/a.o", "build/debug/b.o", "build/debug/c.o" };
    cfg.output_path = "build/debug/libfoo.a";
    cfg.archiver_path = "/usr/bin/ar";

    BuildStaticLibrary task(cfg);

    // inputs: 3 object files
    TEST_ASSERT_EQ((int)task.inputs().size(), 3);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "build/debug/a.o");
    TEST_ASSERT_STR_EQ(task.inputs()[1]->path().c_str(), "build/debug/b.o");
    TEST_ASSERT_STR_EQ(task.inputs()[2]->path().c_str(), "build/debug/c.o");

    // outputs: single library
    TEST_ASSERT_EQ((int)task.outputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.outputs()[0]->path().c_str(), "build/debug/libfoo.a");

    // primaryOutput is the library
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/libfoo.a");

    return 0;
}

TEST(build_static_library_empty_objects) {
    BuildStaticLibrary::Config cfg;
    cfg.output_path = "build/debug/libempty.a";
    cfg.archiver_path = "/usr/bin/ar";

    BuildStaticLibrary task(cfg);

    // inputs: none
    TEST_ASSERT_EQ((int)task.inputs().size(), 0);

    // outputs: single library
    TEST_ASSERT_EQ((int)task.outputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/libempty.a");

    return 0;
}

TEST(build_static_library_condition_is_valid) {
    BuildStaticLibrary::Config cfg;
    cfg.object_paths = { "build/debug/x.o" };
    cfg.output_path = "build/debug/libx.a";
    cfg.archiver_path = "/usr/bin/ar";

    BuildStaticLibrary task(cfg);

    const TaskCondition& cond = task.condition();
    ConditionResult result = cond.evaluate(task.inputs(), task.primaryOutput());
    TEST_ASSERT_EQ((int)result.decision, (int)ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "does not exist");

    return 0;
}

// =============================================================================
// BuildExecutable Tests
// =============================================================================

TEST(build_executable_construction) {
    BuildExecutable::Config cfg;
    cfg.object_paths = { "build/debug/main.o", "build/debug/util.o" };
    cfg.output_path = "build/debug/app.exe";
    cfg.lib_paths = { "build/debug/", "/usr/lib" };
    cfg.link_libs = { "pthread", "m" };
    cfg.linker_path = "/usr/bin/gcc";

    BuildExecutable task(cfg);

    // inputs: 2 object files
    TEST_ASSERT_EQ((int)task.inputs().size(), 2);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "build/debug/main.o");
    TEST_ASSERT_STR_EQ(task.inputs()[1]->path().c_str(), "build/debug/util.o");

    // outputs: single executable
    TEST_ASSERT_EQ((int)task.outputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.outputs()[0]->path().c_str(), "build/debug/app.exe");

    // primaryOutput is the executable
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/app.exe");

    return 0;
}

TEST(build_executable_single_object) {
    BuildExecutable::Config cfg;
    cfg.object_paths = { "build/debug/standalone.o" };
    cfg.output_path = "build/debug/standalone";
    cfg.linker_path = "/usr/bin/gcc";

    BuildExecutable task(cfg);

    TEST_ASSERT_EQ((int)task.inputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "build/debug/standalone.o");
    TEST_ASSERT_EQ((int)task.outputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/standalone");

    return 0;
}

TEST(build_executable_condition_is_valid) {
    BuildExecutable::Config cfg;
    cfg.object_paths = { "build/debug/main.o" };
    cfg.output_path = "build/debug/app";
    cfg.linker_path = "/usr/bin/gcc";

    BuildExecutable task(cfg);

    const TaskCondition& cond = task.condition();
    ConditionResult result = cond.evaluate(task.inputs(), task.primaryOutput());
    TEST_ASSERT_EQ((int)result.decision, (int)ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "does not exist");

    return 0;
}

// =============================================================================
// BuildSharedLibrary Tests
// =============================================================================

TEST(build_shared_library_construction) {
    BuildSharedLibrary::Config cfg;
    cfg.object_paths = { "build/debug/widget.o", "build/debug/render.o" };
    cfg.output_path = "build/debug/libwidget.so";
    cfg.lib_paths = { "/usr/lib" };
    cfg.link_libs = { "GL" };
    cfg.linker_path = "/usr/bin/gcc";

    BuildSharedLibrary task(cfg);

    // inputs: 2 object files
    TEST_ASSERT_EQ((int)task.inputs().size(), 2);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "build/debug/widget.o");
    TEST_ASSERT_STR_EQ(task.inputs()[1]->path().c_str(), "build/debug/render.o");

    // outputs: single shared library
    TEST_ASSERT_EQ((int)task.outputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.outputs()[0]->path().c_str(), "build/debug/libwidget.so");

    // primaryOutput is the shared library
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/libwidget.so");

    return 0;
}

TEST(build_shared_library_same_pattern_as_executable) {
    BuildSharedLibrary::Config cfg;
    cfg.object_paths = { "build/debug/a.o", "build/debug/b.o", "build/debug/c.o" };
    cfg.output_path = "build/debug/libtest.dll";
    cfg.linker_path = "cl.exe";

    BuildSharedLibrary task(cfg);

    // inputs: 3 object files (same pattern as executable)
    TEST_ASSERT_EQ((int)task.inputs().size(), 3);
    TEST_ASSERT_EQ((int)task.outputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/libtest.dll");

    return 0;
}

TEST(build_shared_library_condition_is_valid) {
    BuildSharedLibrary::Config cfg;
    cfg.object_paths = { "build/debug/mod.o" };
    cfg.output_path = "build/debug/libmod.so";
    cfg.linker_path = "/usr/bin/gcc";

    BuildSharedLibrary task(cfg);

    const TaskCondition& cond = task.condition();
    ConditionResult result = cond.evaluate(task.inputs(), task.primaryOutput());
    TEST_ASSERT_EQ((int)result.decision, (int)ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "does not exist");

    return 0;
}

// =============================================================================
// CompileHlslShader Tests
// =============================================================================

TEST(compile_hlsl_shader_construction) {
    CompileHlslShader::Config cfg;
    cfg.source_path = "shaders/vertex.hlsl";
    cfg.output_path = "build/debug/shaders/vertex.dxil";
    cfg.dxc_path = "tools/dxc.exe";
    cfg.entry_point = "VSMain";
    cfg.target_profile = "vs_6_0";

    CompileHlslShader task(cfg);

    // inputs: single shader source
    TEST_ASSERT_EQ((int)task.inputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "shaders/vertex.hlsl");

    // outputs: single .dxil
    TEST_ASSERT_EQ((int)task.outputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.outputs()[0]->path().c_str(), "build/debug/shaders/vertex.dxil");

    // primaryOutput is the .dxil
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/debug/shaders/vertex.dxil");

    return 0;
}

TEST(compile_hlsl_shader_pixel_shader) {
    CompileHlslShader::Config cfg;
    cfg.source_path = "shaders/pixel.hlsl";
    cfg.output_path = "build/release/shaders/pixel.dxil";
    cfg.dxc_path = "tools/dxc.exe";
    cfg.entry_point = "PSMain";
    cfg.target_profile = "ps_6_0";
    cfg.extra_flags = { "-O3", "-Zi" };

    CompileHlslShader task(cfg);

    TEST_ASSERT_EQ((int)task.inputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.inputs()[0]->path().c_str(), "shaders/pixel.hlsl");
    TEST_ASSERT_EQ((int)task.outputs().size(), 1);
    TEST_ASSERT_STR_EQ(task.primaryOutput().path().c_str(), "build/release/shaders/pixel.dxil");

    return 0;
}

TEST(compile_hlsl_shader_condition_is_valid) {
    CompileHlslShader::Config cfg;
    cfg.source_path = "shaders/compute.hlsl";
    cfg.output_path = "build/debug/shaders/compute.dxil";
    cfg.dxc_path = "tools/dxc.exe";
    cfg.entry_point = "CSMain";
    cfg.target_profile = "cs_6_0";

    CompileHlslShader task(cfg);

    const TaskCondition& cond = task.condition();
    ConditionResult result = cond.evaluate(task.inputs(), task.primaryOutput());
    TEST_ASSERT_EQ((int)result.decision, (int)ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "does not exist");

    return 0;
}

// =============================================================================
// MSVC Registration Block
// =============================================================================

#ifdef _MSC_VER
#include <windows.h>

static void __cdecl register_concrete_tasks_tests(void) {
    // BuildCSource
    REGISTER_TEST(build_c_source_construction_no_headers);
    REGISTER_TEST(build_c_source_construction_with_headers);
    REGISTER_TEST(build_c_source_condition_is_valid);
    REGISTER_TEST(build_c_source_forced_condition);
    // BuildCppSource
    REGISTER_TEST(build_cpp_source_construction_no_headers);
    REGISTER_TEST(build_cpp_source_construction_with_headers);
    REGISTER_TEST(build_cpp_source_condition_is_valid);
    // BuildStaticLibrary
    REGISTER_TEST(build_static_library_construction);
    REGISTER_TEST(build_static_library_empty_objects);
    REGISTER_TEST(build_static_library_condition_is_valid);
    // BuildExecutable
    REGISTER_TEST(build_executable_construction);
    REGISTER_TEST(build_executable_single_object);
    REGISTER_TEST(build_executable_condition_is_valid);
    // BuildSharedLibrary
    REGISTER_TEST(build_shared_library_construction);
    REGISTER_TEST(build_shared_library_same_pattern_as_executable);
    REGISTER_TEST(build_shared_library_condition_is_valid);
    // CompileHlslShader
    REGISTER_TEST(compile_hlsl_shader_construction);
    REGISTER_TEST(compile_hlsl_shader_pixel_shader);
    REGISTER_TEST(compile_hlsl_shader_condition_is_valid);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void(__cdecl* _reg_concrete_tasks)(void) = register_concrete_tasks_tests;
#endif
