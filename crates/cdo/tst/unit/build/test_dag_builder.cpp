// test_dag_builder.cpp — Unit tests for DAG builder (workspace → TasksDag construction).
// Validates: Requirements 4.5, 11.1, 11.2
//
// Creates mock Workspace/Crate/Module C structs (no filesystem needed) and
// calls build_dag(). Inspects the resulting TasksDag for correct task count,
// types, and dependency edges via dispatch order.
//
// Key invariants tested:
//   - Compile tasks are leaf tasks (dispatchable immediately)
//   - Link/archive depends on all compile tasks within the same module
//   - exe/tst/e2e/dyn link tasks depend on same-crate lib archive
//   - Inter-crate: dependent crate's link depends on dependency's lib archive
//   - Shader modules produce CompileHlslShader tasks with no link task
//   - No filesystem operations during construction

#include "cdo_ut.h"
#include "build/dag_builder.h"
#include "build/tasks_dag.h"
#include "build/task.h"
#include "build/artifact.h"
#include "build/tasks/build_c_source.h"
#include "build/tasks/build_cpp_source.h"
#include "build/tasks/build_static_library.h"
#include "build/tasks/build_executable.h"
#include "build/tasks/build_shared_library.h"
#include "build/tasks/compile_hlsl_shader.h"

#include <cstring>
#include <string>
#include <vector>
#include <set>

using namespace cdo::build;

// ---------------------------------------------------------------------------
// Helper: Build a default DagBuildConfig for testing
// ---------------------------------------------------------------------------

static DagBuildConfig make_test_config() {
    DagBuildConfig cfg;
    cfg.profile = "debug";
    cfg.force = false;
    cfg.compiler_path = "/usr/bin/gcc";
    cfg.compiler_family = 0;
    cfg.archiver_path = "/usr/bin/ar";
    cfg.dxc_path = "tools/dxc.exe";
    return cfg;
}

// ---------------------------------------------------------------------------
// Helper: Populate a Module with fake source file paths
// ---------------------------------------------------------------------------

static void init_module(Module* mod, ModuleKind kind, const char* dir_path,
                        const char** source_paths, int source_count) {
    memset(mod, 0, sizeof(Module));
    mod->kind = kind;
    mod->present = true;
    strncpy(mod->dir_path, dir_path, sizeof(mod->dir_path) - 1);

    if (source_count > 0) {
        mod->sources.paths = (char**)malloc(sizeof(char*) * source_count);
        mod->sources.count = source_count;
        mod->sources.capacity = source_count;
        for (int i = 0; i < source_count; i++) {
            mod->sources.paths[i] = strdup(source_paths[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// Helper: Free module sources
// ---------------------------------------------------------------------------

static void free_module_sources(Module* mod) {
    for (int i = 0; i < mod->sources.count; i++) {
        free(mod->sources.paths[i]);
    }
    free(mod->sources.paths);
    mod->sources.paths = nullptr;
    mod->sources.count = 0;
    mod->sources.capacity = 0;
}

// ---------------------------------------------------------------------------
// Helper: Initialize a Crate with given name and modules
// ---------------------------------------------------------------------------

static void init_crate(Crate* crate, const char* name, const char* path) {
    memset(crate, 0, sizeof(Crate));
    strncpy(crate->name, name, sizeof(crate->name) - 1);
    strncpy(crate->path, path, sizeof(crate->path) - 1);
    crate->c_standard = 17;
    crate->cpp_standard = 17;
    // All modules start as not present
    for (int i = 0; i < MODULE_KIND_COUNT; i++) {
        crate->modules[i].present = false;
        crate->modules[i].kind = (ModuleKind)i;
    }
}

// ---------------------------------------------------------------------------
// Helper: Free a crate's resources
// ---------------------------------------------------------------------------

static void free_crate(Crate* crate) {
    for (int i = 0; i < MODULE_KIND_COUNT; i++) {
        if (crate->modules[i].present) {
            free_module_sources(&crate->modules[i]);
        }
    }
    free(crate->dep_indices);
    free(crate->dev_dep_indices);
}

// ---------------------------------------------------------------------------
// Helper: Collect all tasks from a finalized DAG by dispatching them.
// Returns task pointers in dispatch order. Marks each completed immediately.
// ---------------------------------------------------------------------------

static std::vector<Task*> drain_dag(TasksDag& dag) {
    std::vector<Task*> tasks;
    while (dag.hasActiveTask()) {
        Task* t = dag.waitNextTask();
        if (!t) break;
        tasks.push_back(t);
        dag.markCompleted(t->id());
    }
    return tasks;
}

// ---------------------------------------------------------------------------
// Helper: Check if a task's primary output path contains a substring
// ---------------------------------------------------------------------------

static bool task_output_contains(const Task* t, const char* substr) {
    return t->primaryOutput().path().find(substr) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Helper: Check if task is a specific type via dynamic_cast
// ---------------------------------------------------------------------------

static bool is_build_c_source(const Task* t) {
    return dynamic_cast<const BuildCSource*>(t) != nullptr;
}
static bool is_build_cpp_source(const Task* t) {
    return dynamic_cast<const BuildCppSource*>(t) != nullptr;
}
static bool is_build_static_library(const Task* t) {
    return dynamic_cast<const BuildStaticLibrary*>(t) != nullptr;
}
static bool is_build_executable(const Task* t) {
    return dynamic_cast<const BuildExecutable*>(t) != nullptr;
}
static bool is_build_shared_library(const Task* t) {
    return dynamic_cast<const BuildSharedLibrary*>(t) != nullptr;
}
static bool is_compile_hlsl_shader(const Task* t) {
    return dynamic_cast<const CompileHlslShader*>(t) != nullptr;
}

// ---------------------------------------------------------------------------
// Test: single crate with lib module → compile tasks + one archive task
// ---------------------------------------------------------------------------

TEST(dag_builder_single_crate_lib_module) {
    // Setup: one crate "mylib" with lib/ containing 2 .c files
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    strncpy(ws.root_path, "C:/fake/workspace", sizeof(ws.root_path) - 1);

    Crate crate;
    init_crate(&crate, "mylib", "crates/mylib");

    const char* lib_sources[] = { "crates/mylib/lib/foo.c", "crates/mylib/lib/bar.c" };
    init_module(&crate.modules[MODULE_LIB], MODULE_LIB, "crates/mylib/lib", lib_sources, 2);
    crate.has_lib = true;
    crate.module_count = 1;

    ws.crates = &crate;
    ws.crate_count = 1;
    int build_order[] = { 0 };
    ws.build_order = build_order;
    ws.build_order_count = 1;

    // Build DAG
    DagBuildConfig config = make_test_config();
    TasksDag dag;
    int rc = build_dag(&ws, config, dag);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Expected: 2 compile tasks (BuildCSource) + 1 archive task (BuildStaticLibrary) = 3 total
    TEST_ASSERT_EQ(dag.totalCount(), 3);

    // Drain the DAG — compile tasks should dispatch first (they're leaves)
    std::vector<Task*> tasks = drain_dag(dag);
    TEST_ASSERT_EQ((int)tasks.size(), 3);

    // First two tasks dispatched should be compile tasks (order among them is arbitrary)
    TEST_ASSERT(is_build_c_source(tasks[0]) || is_build_c_source(tasks[1]));
    TEST_ASSERT(is_build_c_source(tasks[0]) && is_build_c_source(tasks[1]));

    // Last task should be the archive (BuildStaticLibrary)
    TEST_ASSERT(is_build_static_library(tasks[2]));

    // Archive output should reference the lib artifact
    TEST_ASSERT(task_output_contains(tasks[2], "mylib"));

    free_crate(&crate);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: crate with lib + exe → compile tasks for both, exe link depends on lib archive
// ---------------------------------------------------------------------------

TEST(dag_builder_lib_plus_exe_dependency) {
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    strncpy(ws.root_path, "C:/fake/workspace", sizeof(ws.root_path) - 1);

    Crate crate;
    init_crate(&crate, "myapp", "crates/myapp");

    // lib module: 1 source
    const char* lib_sources[] = { "crates/myapp/lib/core.c" };
    init_module(&crate.modules[MODULE_LIB], MODULE_LIB, "crates/myapp/lib", lib_sources, 1);
    crate.has_lib = true;

    // exe module: 1 source
    const char* exe_sources[] = { "crates/myapp/exe/main.c" };
    init_module(&crate.modules[MODULE_EXE], MODULE_EXE, "crates/myapp/exe", exe_sources, 1);
    crate.module_count = 2;

    ws.crates = &crate;
    ws.crate_count = 1;
    int build_order[] = { 0 };
    ws.build_order = build_order;
    ws.build_order_count = 1;

    DagBuildConfig config = make_test_config();
    TasksDag dag;
    int rc = build_dag(&ws, config, dag);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Expected: lib compile(1) + lib archive(1) + exe compile(1) + exe link(1) = 4
    TEST_ASSERT_EQ(dag.totalCount(), 4);

    // Drain and verify ordering constraints
    std::vector<Task*> tasks = drain_dag(dag);
    TEST_ASSERT_EQ((int)tasks.size(), 4);

    // Find the lib archive and exe link tasks
    Task* lib_archive = nullptr;
    Task* exe_link = nullptr;
    int compile_count = 0;

    for (auto* t : tasks) {
        if (is_build_static_library(t)) lib_archive = t;
        else if (is_build_executable(t)) exe_link = t;
        else if (is_build_c_source(t)) compile_count++;
    }

    TEST_ASSERT(lib_archive != nullptr);
    TEST_ASSERT(exe_link != nullptr);
    TEST_ASSERT_EQ(compile_count, 2);

    // Verify ordering: lib_archive dispatches before exe_link
    // In a topologically ordered drain, the archive must come before the exe link
    int lib_archive_pos = -1;
    int exe_link_pos = -1;
    for (int i = 0; i < (int)tasks.size(); i++) {
        if (tasks[i] == lib_archive) lib_archive_pos = i;
        if (tasks[i] == exe_link) exe_link_pos = i;
    }
    TEST_ASSERT(lib_archive_pos < exe_link_pos);

    free_crate(&crate);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: multi-crate with inter-crate dependency →
//       dependent crate's link depends on dependency's lib archive
// ---------------------------------------------------------------------------

TEST(dag_builder_multi_crate_inter_dependency) {
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    strncpy(ws.root_path, "C:/fake/workspace", sizeof(ws.root_path) - 1);

    // Crate 0: "base" (library, no deps)
    Crate crates[2];
    init_crate(&crates[0], "base", "crates/base");
    const char* base_lib_src[] = { "crates/base/lib/base.c" };
    init_module(&crates[0].modules[MODULE_LIB], MODULE_LIB, "crates/base/lib", base_lib_src, 1);
    crates[0].has_lib = true;
    crates[0].module_count = 1;

    // Crate 1: "app" (exe, depends on base)
    init_crate(&crates[1], "app", "crates/app");
    const char* app_exe_src[] = { "crates/app/exe/main.c" };
    init_module(&crates[1].modules[MODULE_EXE], MODULE_EXE, "crates/app/exe", app_exe_src, 1);
    crates[1].module_count = 1;

    // Set up dependency: app depends on base
    int app_deps[] = { 0 };
    crates[1].dep_count = 1;
    crates[1].dep_indices = app_deps;

    ws.crates = crates;
    ws.crate_count = 2;
    // Build order: base first, then app
    int build_order[] = { 0, 1 };
    ws.build_order = build_order;
    ws.build_order_count = 2;

    DagBuildConfig config = make_test_config();
    TasksDag dag;
    int rc = build_dag(&ws, config, dag);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Expected: base_compile(1) + base_archive(1) + app_compile(1) + app_link(1) = 4
    TEST_ASSERT_EQ(dag.totalCount(), 4);

    std::vector<Task*> tasks = drain_dag(dag);
    TEST_ASSERT_EQ((int)tasks.size(), 4);

    // Find base's lib archive and app's exe link
    Task* base_archive = nullptr;
    Task* app_link = nullptr;
    for (auto* t : tasks) {
        if (is_build_static_library(t) && task_output_contains(t, "base")) {
            base_archive = t;
        }
        if (is_build_executable(t) && task_output_contains(t, "app")) {
            app_link = t;
        }
    }

    TEST_ASSERT(base_archive != nullptr);
    TEST_ASSERT(app_link != nullptr);

    // base_archive must dispatch before app_link (inter-crate dependency)
    int base_archive_pos = -1;
    int app_link_pos = -1;
    for (int i = 0; i < (int)tasks.size(); i++) {
        if (tasks[i] == base_archive) base_archive_pos = i;
        if (tasks[i] == app_link) app_link_pos = i;
    }
    TEST_ASSERT(base_archive_pos < app_link_pos);

    // Don't free dep_indices since we used stack array
    crates[1].dep_indices = nullptr;
    crates[1].dep_count = 0;
    free_crate(&crates[0]);
    free_crate(&crates[1]);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: all module kinds (exe, lib, tst, e2e, dyn, shd) produce correct task types
// ---------------------------------------------------------------------------

TEST(dag_builder_all_module_kinds_correct_types) {
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    strncpy(ws.root_path, "C:/fake/workspace", sizeof(ws.root_path) - 1);

    Crate crate;
    init_crate(&crate, "full", "crates/full");

    // lib module: 1 source
    const char* lib_src[] = { "crates/full/lib/lib.c" };
    init_module(&crate.modules[MODULE_LIB], MODULE_LIB, "crates/full/lib", lib_src, 1);
    crate.has_lib = true;

    // exe module: 1 source
    const char* exe_src[] = { "crates/full/exe/main.c" };
    init_module(&crate.modules[MODULE_EXE], MODULE_EXE, "crates/full/exe", exe_src, 1);

    // tst module: 1 source
    const char* tst_src[] = { "crates/full/tst/test.c" };
    init_module(&crate.modules[MODULE_TST], MODULE_TST, "crates/full/tst", tst_src, 1);

    // e2e module: 1 source
    const char* e2e_src[] = { "crates/full/e2e/e2e.c" };
    init_module(&crate.modules[MODULE_E2E], MODULE_E2E, "crates/full/e2e", e2e_src, 1);

    // dyn module: 1 source
    const char* dyn_src[] = { "crates/full/dyn/plugin.c" };
    init_module(&crate.modules[MODULE_DYN], MODULE_DYN, "crates/full/dyn", dyn_src, 1);

    // shd module: 1 source
    const char* shd_src[] = { "crates/full/shd/vertex.hlsl" };
    init_module(&crate.modules[MODULE_SHD], MODULE_SHD, "crates/full/shd", shd_src, 1);
    crate.has_shd = true;

    crate.module_count = 6;

    ws.crates = &crate;
    ws.crate_count = 1;
    int build_order[] = { 0 };
    ws.build_order = build_order;
    ws.build_order_count = 1;

    DagBuildConfig config = make_test_config();
    TasksDag dag;
    int rc = build_dag(&ws, config, dag);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Expected tasks:
    //   lib: 1 compile + 1 archive = 2
    //   exe: 1 compile + 1 link = 2
    //   tst: 1 compile + 1 link = 2
    //   e2e: 1 compile + 1 link = 2
    //   dyn: 1 compile + 1 link = 2
    //   shd: 1 shader compile (no link) = 1
    //   Total = 11
    TEST_ASSERT_EQ(dag.totalCount(), 11);

    std::vector<Task*> tasks = drain_dag(dag);
    TEST_ASSERT_EQ((int)tasks.size(), 11);

    // Count task types
    int c_source_count = 0;
    int static_lib_count = 0;
    int executable_count = 0;
    int shared_lib_count = 0;
    int shader_count = 0;

    for (auto* t : tasks) {
        if (is_build_c_source(t)) c_source_count++;
        else if (is_build_static_library(t)) static_lib_count++;
        else if (is_build_executable(t)) executable_count++;
        else if (is_build_shared_library(t)) shared_lib_count++;
        else if (is_compile_hlsl_shader(t)) shader_count++;
    }

    // 6 compile tasks (one per module with a .c source): lib, exe, tst, e2e, dyn, shd(hlsl)
    // Actually shd uses CompileHlslShader not BuildCSource
    TEST_ASSERT_EQ(c_source_count, 5);      // lib, exe, tst, e2e, dyn
    TEST_ASSERT_EQ(static_lib_count, 1);    // lib archive
    TEST_ASSERT_EQ(executable_count, 3);    // exe, tst, e2e
    TEST_ASSERT_EQ(shared_lib_count, 1);    // dyn
    TEST_ASSERT_EQ(shader_count, 1);        // shd

    free_crate(&crate);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader module produces CompileHlslShader tasks with no link task
// ---------------------------------------------------------------------------

TEST(dag_builder_shader_module_no_link) {
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    strncpy(ws.root_path, "C:/fake/workspace", sizeof(ws.root_path) - 1);

    Crate crate;
    init_crate(&crate, "shaders", "crates/shaders");

    // shd module: 3 shader files
    const char* shd_src[] = {
        "crates/shaders/shd/vertex.hlsl",
        "crates/shaders/shd/pixel.hlsl",
        "crates/shaders/shd/compute.hlsl"
    };
    init_module(&crate.modules[MODULE_SHD], MODULE_SHD, "crates/shaders/shd", shd_src, 3);
    crate.has_shd = true;
    crate.module_count = 1;

    ws.crates = &crate;
    ws.crate_count = 1;
    int build_order[] = { 0 };
    ws.build_order = build_order;
    ws.build_order_count = 1;

    DagBuildConfig config = make_test_config();
    TasksDag dag;
    int rc = build_dag(&ws, config, dag);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // 3 CompileHlslShader tasks, no link task
    TEST_ASSERT_EQ(dag.totalCount(), 3);

    std::vector<Task*> tasks = drain_dag(dag);
    TEST_ASSERT_EQ((int)tasks.size(), 3);

    // All tasks should be CompileHlslShader
    for (auto* t : tasks) {
        TEST_ASSERT(is_compile_hlsl_shader(t));
    }

    // No static lib, no executable, no shared lib
    for (auto* t : tasks) {
        TEST_ASSERT(!is_build_static_library(t));
        TEST_ASSERT(!is_build_executable(t));
        TEST_ASSERT(!is_build_shared_library(t));
    }

    free_crate(&crate);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: no filesystem operations during construction (mock workspace model)
// This test verifies that build_dag() can operate on a purely in-memory
// workspace model with fake paths that don't exist on disk. If the DAG
// builder attempted any filesystem operations, it would fail or behave
// differently for non-existent paths.
// ---------------------------------------------------------------------------

TEST(dag_builder_no_filesystem_operations) {
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    // Use a path that definitely doesn't exist
    strncpy(ws.root_path, "Z:/nonexistent/workspace/path", sizeof(ws.root_path) - 1);

    Crate crate;
    init_crate(&crate, "phantom", "crates/phantom");

    // Source files that don't exist on disk
    const char* lib_src[] = {
        "Z:/nonexistent/phantom/lib/ghost.c",
        "Z:/nonexistent/phantom/lib/specter.cpp"
    };
    init_module(&crate.modules[MODULE_LIB], MODULE_LIB, "Z:/nonexistent/phantom/lib", lib_src, 2);
    crate.has_lib = true;
    crate.module_count = 1;

    ws.crates = &crate;
    ws.crate_count = 1;
    int build_order[] = { 0 };
    ws.build_order = build_order;
    ws.build_order_count = 1;

    DagBuildConfig config = make_test_config();
    TasksDag dag;
    // This should succeed without touching the filesystem
    int rc = build_dag(&ws, config, dag);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Should still produce tasks (2 compile + 1 archive = 3)
    TEST_ASSERT_EQ(dag.totalCount(), 3);

    // Drain to confirm all tasks are reachable
    std::vector<Task*> tasks = drain_dag(dag);
    TEST_ASSERT_EQ((int)tasks.size(), 3);

    free_crate(&crate);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: crate with lib + tst → tst link depends on lib archive
// (Same pattern as exe, but verifies tst module produces BuildExecutable)
// ---------------------------------------------------------------------------

TEST(dag_builder_tst_depends_on_lib_archive) {
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    strncpy(ws.root_path, "C:/fake/workspace", sizeof(ws.root_path) - 1);

    Crate crate;
    init_crate(&crate, "mytest", "crates/mytest");

    const char* lib_src[] = { "crates/mytest/lib/impl.c" };
    init_module(&crate.modules[MODULE_LIB], MODULE_LIB, "crates/mytest/lib", lib_src, 1);
    crate.has_lib = true;

    const char* tst_src[] = { "crates/mytest/tst/test_impl.c" };
    init_module(&crate.modules[MODULE_TST], MODULE_TST, "crates/mytest/tst", tst_src, 1);
    crate.module_count = 2;

    ws.crates = &crate;
    ws.crate_count = 1;
    int build_order[] = { 0 };
    ws.build_order = build_order;
    ws.build_order_count = 1;

    DagBuildConfig config = make_test_config();
    TasksDag dag;
    int rc = build_dag(&ws, config, dag);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // lib compile(1) + lib archive(1) + tst compile(1) + tst link(1) = 4
    TEST_ASSERT_EQ(dag.totalCount(), 4);

    std::vector<Task*> tasks = drain_dag(dag);

    // Find the lib archive and tst link
    Task* lib_archive = nullptr;
    Task* tst_link = nullptr;
    for (auto* t : tasks) {
        if (is_build_static_library(t)) lib_archive = t;
        else if (is_build_executable(t)) tst_link = t;
    }

    TEST_ASSERT(lib_archive != nullptr);
    TEST_ASSERT(tst_link != nullptr);

    // lib_archive must come before tst_link in dispatch order
    int lib_pos = -1, tst_pos = -1;
    for (int i = 0; i < (int)tasks.size(); i++) {
        if (tasks[i] == lib_archive) lib_pos = i;
        if (tasks[i] == tst_link) tst_pos = i;
    }
    TEST_ASSERT(lib_pos < tst_pos);

    free_crate(&crate);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: crate with lib + dyn → dyn link depends on lib archive
// ---------------------------------------------------------------------------

TEST(dag_builder_dyn_depends_on_lib_archive) {
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    strncpy(ws.root_path, "C:/fake/workspace", sizeof(ws.root_path) - 1);

    Crate crate;
    init_crate(&crate, "plugin", "crates/plugin");

    const char* lib_src[] = { "crates/plugin/lib/core.c" };
    init_module(&crate.modules[MODULE_LIB], MODULE_LIB, "crates/plugin/lib", lib_src, 1);
    crate.has_lib = true;

    const char* dyn_src[] = { "crates/plugin/dyn/exports.c" };
    init_module(&crate.modules[MODULE_DYN], MODULE_DYN, "crates/plugin/dyn", dyn_src, 1);
    crate.module_count = 2;

    ws.crates = &crate;
    ws.crate_count = 1;
    int build_order[] = { 0 };
    ws.build_order = build_order;
    ws.build_order_count = 1;

    DagBuildConfig config = make_test_config();
    TasksDag dag;
    int rc = build_dag(&ws, config, dag);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // lib compile(1) + lib archive(1) + dyn compile(1) + dyn link(1) = 4
    TEST_ASSERT_EQ(dag.totalCount(), 4);

    std::vector<Task*> tasks = drain_dag(dag);

    Task* lib_archive = nullptr;
    Task* dyn_link = nullptr;
    for (auto* t : tasks) {
        if (is_build_static_library(t)) lib_archive = t;
        else if (is_build_shared_library(t)) dyn_link = t;
    }

    TEST_ASSERT(lib_archive != nullptr);
    TEST_ASSERT(dyn_link != nullptr);

    // lib_archive must come before dyn_link
    int lib_pos = -1, dyn_pos = -1;
    for (int i = 0; i < (int)tasks.size(); i++) {
        if (tasks[i] == lib_archive) lib_pos = i;
        if (tasks[i] == dyn_link) dyn_pos = i;
    }
    TEST_ASSERT(lib_pos < dyn_pos);

    free_crate(&crate);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cpp source files produce BuildCppSource tasks
// ---------------------------------------------------------------------------

TEST(dag_builder_cpp_sources_produce_cpp_tasks) {
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    strncpy(ws.root_path, "C:/fake/workspace", sizeof(ws.root_path) - 1);

    Crate crate;
    init_crate(&crate, "cpplib", "crates/cpplib");

    // lib module with .cpp files
    const char* lib_src[] = { "crates/cpplib/lib/engine.cpp", "crates/cpplib/lib/renderer.cpp" };
    init_module(&crate.modules[MODULE_LIB], MODULE_LIB, "crates/cpplib/lib", lib_src, 2);
    crate.has_lib = true;
    crate.module_count = 1;

    ws.crates = &crate;
    ws.crate_count = 1;
    int build_order[] = { 0 };
    ws.build_order = build_order;
    ws.build_order_count = 1;

    DagBuildConfig config = make_test_config();
    TasksDag dag;
    int rc = build_dag(&ws, config, dag);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // 2 compile + 1 archive = 3
    TEST_ASSERT_EQ(dag.totalCount(), 3);

    std::vector<Task*> tasks = drain_dag(dag);

    int cpp_count = 0;
    for (auto* t : tasks) {
        if (is_build_cpp_source(t)) cpp_count++;
    }
    TEST_ASSERT_EQ(cpp_count, 2);

    free_crate(&crate);
    return 0;
}

// ---------------------------------------------------------------------------
// MSVC registration block
// ---------------------------------------------------------------------------

#ifdef _MSC_VER
#include <windows.h>

static void __cdecl register_dag_builder_tests(void) {
    REGISTER_TEST(dag_builder_single_crate_lib_module);
    REGISTER_TEST(dag_builder_lib_plus_exe_dependency);
    REGISTER_TEST(dag_builder_multi_crate_inter_dependency);
    REGISTER_TEST(dag_builder_all_module_kinds_correct_types);
    REGISTER_TEST(dag_builder_shader_module_no_link);
    REGISTER_TEST(dag_builder_no_filesystem_operations);
    REGISTER_TEST(dag_builder_tst_depends_on_lib_archive);
    REGISTER_TEST(dag_builder_dyn_depends_on_lib_archive);
    REGISTER_TEST(dag_builder_cpp_sources_produce_cpp_tasks);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void(__cdecl* _reg_dag_builder)(void) = register_dag_builder_tests;
#endif
