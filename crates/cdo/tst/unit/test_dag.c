// crates/cdo/tst/unit/test_dag.c
// Unit tests for DAG generation (model/dag.c)
// Tests dag_generate, dag_graph_create, dag_graph_free, dag_graph_finalize
#include "cdo_ut.h"
#include "model/dag.h"
#include "model/workspace.h"
#include "model/module.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Helpers: build mock workspaces for DAG generation tests
// ---------------------------------------------------------------------------

typedef struct {
    char ws_root[260];
    Workspace ws;
    Crate crates[4];
    int dep_indices[4][4];
    int build_order[4];
} DagTestFixture;

static void dag_fixture_init(DagTestFixture* f, int crate_count) {
    memset(f, 0, sizeof(DagTestFixture));

    // Use a temp directory as workspace root so pal_mkdir_p works
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(f->ws_root, sizeof(f->ws_root), "%s/cdo_test_dag", tmp);
    pal_path_normalize(f->ws_root);

    strncpy(f->ws.root_path, f->ws_root, sizeof(f->ws.root_path) - 1);
    f->ws.crate_count = crate_count;
    f->ws.crates = f->crates;
    f->ws.build_order = f->build_order;
    f->ws.build_order_count = crate_count;

    // Default build order: 0, 1, 2, ... (caller can override)
    for (int i = 0; i < crate_count; i++) {
        f->build_order[i] = i;
    }
}

/// Set up a crate with lib/ module containing fake source files.
static void dag_fixture_add_lib(DagTestFixture* f, int crate_idx, const char* name, const char** sources, int source_count) {
    Crate* c = &f->crates[crate_idx];
    strncpy(c->name, name, sizeof(c->name) - 1);
    strncpy(c->path, name, sizeof(c->path) - 1);

    c->has_lib = true;
    c->modules[MODULE_LIB].present = true;
    c->modules[MODULE_LIB].kind = MODULE_LIB;
    snprintf(c->modules[MODULE_LIB].dir_path, sizeof(c->modules[MODULE_LIB].dir_path), "%s/%s/lib", f->ws_root, name);

    c->modules[MODULE_LIB].sources.count = source_count;
    c->modules[MODULE_LIB].sources.capacity = source_count;
    if (source_count > 0) {
        c->modules[MODULE_LIB].sources.paths = (char**)malloc((size_t)source_count * sizeof(char*));
        for (int i = 0; i < source_count; i++) {
            c->modules[MODULE_LIB].sources.paths[i] = strdup(sources[i]);
        }
    }
    c->module_count++;
}

/// Set up exe/ module on a crate with fake source files.
static void dag_fixture_add_exe(DagTestFixture* f, int crate_idx, const char** sources, int source_count) {
    Crate* c = &f->crates[crate_idx];

    c->modules[MODULE_EXE].present = true;
    c->modules[MODULE_EXE].kind = MODULE_EXE;
    snprintf(c->modules[MODULE_EXE].dir_path, sizeof(c->modules[MODULE_EXE].dir_path), "%s/%s/exe", f->ws_root, c->name);

    c->modules[MODULE_EXE].sources.count = source_count;
    c->modules[MODULE_EXE].sources.capacity = source_count;
    if (source_count > 0) {
        c->modules[MODULE_EXE].sources.paths = (char**)malloc((size_t)source_count * sizeof(char*));
        for (int i = 0; i < source_count; i++) {
            c->modules[MODULE_EXE].sources.paths[i] = strdup(sources[i]);
        }
    }
    c->module_count++;
}

/// Set a dependency from crate at src_idx to crate at dep_idx.
static void dag_fixture_add_dep(DagTestFixture* f, int src_idx, int dep_idx) {
    Crate* c = &f->crates[src_idx];
    f->dep_indices[src_idx][c->dep_count] = dep_idx;
    c->dep_indices = f->dep_indices[src_idx];
    c->dep_count++;
}

/// Free the allocated source paths in the fixture (but NOT crate arrays themselves since they're stack).
static void dag_fixture_destroy(DagTestFixture* f) {
    for (int i = 0; i < f->ws.crate_count; i++) {
        for (int m = 0; m < MODULE_KIND_COUNT; m++) {
            FileList* fl = &f->crates[i].modules[m].sources;
            if (fl->paths) {
                for (int p = 0; p < fl->count; p++) {
                    free(fl->paths[p]);
                }
                free(fl->paths);
                fl->paths = NULL;
            }
        }
    }
}

/// Count tasks of a given kind in the graph.
static int count_tasks_by_kind(const DagGraph* graph, DagTaskKind kind) {
    return dag_graph_task_count_by_kind(graph, kind);
}

/// Find a task by kind, crate_idx, and module_kind. Returns task ID or -1.
static int find_task(const DagGraph* graph, DagTaskKind kind, int crate_idx, ModuleKind mod_kind) {
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == kind && graph->tasks[i].crate_idx == crate_idx && graph->tasks[i].module_kind == mod_kind) {
            return i;
        }
    }
    return -1;
}

/// Check if task_id depends on dep_id (direct dependency).
static bool task_depends_on(const DagGraph* graph, int task_id, int dep_id) {
    if (task_id < 0 || task_id >= graph->task_count) return false;
    const DagTask* task = &graph->tasks[task_id];
    for (int i = 0; i < task->dep_count; i++) {
        if (task->dep_ids[i] == dep_id) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Test 1: dag_graph_create and dag_graph_free work without memory issues
// ---------------------------------------------------------------------------

TEST(dag_graph_create_and_free) {
    DagGraph* graph = dag_graph_create(16);
    TEST_ASSERT(graph != NULL);
    TEST_ASSERT(graph->task_count == 0);
    TEST_ASSERT(graph->task_capacity >= 16);
    TEST_ASSERT(graph->tasks != NULL);
    dag_graph_free(graph);
    return 0;
}

TEST(dag_graph_create_zero_capacity_uses_default) {
    DagGraph* graph = dag_graph_create(0);
    TEST_ASSERT(graph != NULL);
    TEST_ASSERT(graph->task_capacity >= 1);
    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 2: Single crate with lib/ only -> 1 link task, N compile tasks
// ---------------------------------------------------------------------------

TEST(dag_single_crate_lib_only) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    const char* sources[] = { "/fake/src/foo.c", "/fake/src/bar.c", "/fake/src/baz.c" };
    dag_fixture_add_lib(&f, 0, "mylib", sources, 3);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(graph != NULL);

    // Should have exactly 3 compile tasks and 1 link task
    int compile_count = count_tasks_by_kind(graph, DAG_TASK_COMPILE);
    int link_count = count_tasks_by_kind(graph, DAG_TASK_LINK);
    TEST_ASSERT_EQ(compile_count, 3);
    TEST_ASSERT_EQ(link_count, 1);

    // Find the link task for lib/
    int lib_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_LIB);
    TEST_ASSERT(lib_link >= 0);

    // Link task should depend on all 3 compile tasks
    TEST_ASSERT_EQ(graph->tasks[lib_link].dep_count, 3);

    // Compile tasks should have source_path filled
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == DAG_TASK_COMPILE) {
            TEST_ASSERT(strlen(graph->tasks[i].source_path) > 0);
            TEST_ASSERT(strlen(graph->tasks[i].object_path) > 0);
        }
    }

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 3: Single crate with lib/ + exe/ -> correct tasks and deps
// ---------------------------------------------------------------------------

TEST(dag_single_crate_lib_and_exe) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    const char* lib_sources[] = { "/fake/lib/a.c", "/fake/lib/b.c" };
    dag_fixture_add_lib(&f, 0, "myapp", lib_sources, 2);

    const char* exe_sources[] = { "/fake/exe/main.c" };
    dag_fixture_add_exe(&f, 0, exe_sources, 1);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(graph != NULL);

    // 2 lib compile + 1 exe compile = 3 compile tasks
    int compile_count = count_tasks_by_kind(graph, DAG_TASK_COMPILE);
    TEST_ASSERT_EQ(compile_count, 3);

    // 1 lib link + 1 exe link = 2 link tasks
    int link_count = count_tasks_by_kind(graph, DAG_TASK_LINK);
    TEST_ASSERT_EQ(link_count, 2);

    // Find tasks
    int lib_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_LIB);
    int exe_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_EXE);
    TEST_ASSERT(lib_link >= 0);
    TEST_ASSERT(exe_link >= 0);

    // exe compile tasks should depend on lib link
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == DAG_TASK_COMPILE && graph->tasks[i].module_kind == MODULE_EXE) {
            TEST_ASSERT(task_depends_on(graph, i, lib_link));
        }
    }

    // exe link should depend on lib link
    TEST_ASSERT(task_depends_on(graph, exe_link, lib_link));

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 4: 2 crates (B depends on A) -> B compile has NO dep on A link;
//         B link depends on A link
// ---------------------------------------------------------------------------

TEST(dag_two_crates_dep_edges) {
    DagTestFixture f;
    dag_fixture_init(&f, 2);

    // Crate 0: "base" with lib/
    const char* base_sources[] = { "/fake/base/lib/core.c" };
    dag_fixture_add_lib(&f, 0, "base", base_sources, 1);

    // Crate 1: "app" with lib/, depends on base
    const char* app_sources[] = { "/fake/app/lib/app.c" };
    dag_fixture_add_lib(&f, 1, "app", app_sources, 1);
    dag_fixture_add_dep(&f, 1, 0); // app depends on base

    // Build order: base first, then app
    f.build_order[0] = 0;
    f.build_order[1] = 1;

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(graph != NULL);

    // Find link tasks
    int base_lib_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_LIB);
    int app_lib_link = find_task(graph, DAG_TASK_LINK, 1, MODULE_LIB);
    TEST_ASSERT(base_lib_link >= 0);
    TEST_ASSERT(app_lib_link >= 0);

    // B's (app) link should depend on A's (base) link
    TEST_ASSERT(task_depends_on(graph, app_lib_link, base_lib_link));

    // B's compile tasks should NOT depend on A's link task
    // (key DAG insight: compile only needs headers, not linked artifacts)
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == DAG_TASK_COMPILE && graph->tasks[i].crate_idx == 1) {
            TEST_ASSERT(!task_depends_on(graph, i, base_lib_link));
        }
    }

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 5: Crate with no lib/ (exe-only) -> compile tasks have no lib link dep
// ---------------------------------------------------------------------------

TEST(dag_exe_only_crate_no_lib_dep) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    // Set up crate with only exe/ module (no lib/)
    Crate* c = &f.crates[0];
    strncpy(c->name, "tool", sizeof(c->name) - 1);
    strncpy(c->path, "tool", sizeof(c->path) - 1);

    const char* exe_sources[] = { "/fake/tool/exe/main.c", "/fake/tool/exe/util.c" };
    dag_fixture_add_exe(&f, 0, exe_sources, 2);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(graph != NULL);

    // Should have 2 compile tasks and 1 link task (for exe/)
    int compile_count = count_tasks_by_kind(graph, DAG_TASK_COMPILE);
    int link_count = count_tasks_by_kind(graph, DAG_TASK_LINK);
    TEST_ASSERT_EQ(compile_count, 2);
    TEST_ASSERT_EQ(link_count, 1);

    // No lib link task should exist
    int lib_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_LIB);
    TEST_ASSERT_EQ(lib_link, -1);

    // Compile tasks should have no dep on any link task (no lib/)
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == DAG_TASK_COMPILE) {
            for (int d = 0; d < graph->tasks[i].dep_count; d++) {
                int dep_id = graph->tasks[i].dep_ids[d];
                TEST_ASSERT(graph->tasks[dep_id].kind != DAG_TASK_LINK);
            }
        }
    }

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 6: Empty crate (no sources) -> no compile/link tasks generated
// ---------------------------------------------------------------------------

TEST(dag_empty_crate_no_tasks) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    // Crate with name but no modules marked as present
    Crate* c = &f.crates[0];
    strncpy(c->name, "empty", sizeof(c->name) - 1);
    strncpy(c->path, "empty", sizeof(c->path) - 1);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(graph != NULL);

    // No compile or link tasks
    int compile_count = count_tasks_by_kind(graph, DAG_TASK_COMPILE);
    int link_count = count_tasks_by_kind(graph, DAG_TASK_LINK);
    TEST_ASSERT_EQ(compile_count, 0);
    TEST_ASSERT_EQ(link_count, 0);

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 7: dag_graph_finalize computes correct rdep_ids and remaining_deps
// ---------------------------------------------------------------------------

TEST(dag_finalize_rdeps_and_remaining) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    const char* sources[] = { "/fake/src/a.c", "/fake/src/b.c" };
    dag_fixture_add_lib(&f, 0, "fintest", sources, 2);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(graph != NULL);

    // After dag_generate, finalize has already been called.
    // Verify: compile tasks should have 0 remaining_deps (no crate hook defined)
    // and link task should have remaining_deps == 2 (depends on 2 compile tasks).
    int lib_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_LIB);
    TEST_ASSERT(lib_link >= 0);

    // Link task's remaining_deps should equal its dep_count
    TEST_ASSERT_EQ(graph->tasks[lib_link].remaining_deps, graph->tasks[lib_link].dep_count);
    TEST_ASSERT(graph->tasks[lib_link].remaining_deps >= 2);

    // Compile tasks should have remaining_deps == 0 (no hooks, nothing before them)
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == DAG_TASK_COMPILE) {
            TEST_ASSERT_EQ(graph->tasks[i].remaining_deps, 0);
            TEST_ASSERT_EQ(graph->tasks[i].status, DAG_STATUS_READY);
        }
    }

    // Verify rdep_ids: each compile task should have the link task in its rdep_ids
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == DAG_TASK_COMPILE && graph->tasks[i].crate_idx == 0) {
            bool found_link_in_rdeps = false;
            for (int r = 0; r < graph->tasks[i].rdep_count; r++) {
                if (graph->tasks[i].rdep_ids[r] == lib_link) {
                    found_link_in_rdeps = true;
                    break;
                }
            }
            TEST_ASSERT(found_link_in_rdeps);
        }
    }

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 8: dag_generate with NULL arguments returns error
// ---------------------------------------------------------------------------

TEST(dag_generate_null_args) {
    DagGraph* graph = NULL;
    int rc = dag_generate(NULL, "debug", &graph);
    TEST_ASSERT(rc != 0);
    TEST_ASSERT_NULL(graph);

    Workspace ws = {0};
    rc = dag_generate(&ws, NULL, &graph);
    TEST_ASSERT(rc != 0);

    rc = dag_generate(&ws, "debug", NULL);
    TEST_ASSERT(rc != 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 9: lib/ present but with zero compilable sources -> link task still
//         created but with zero compile deps
// ---------------------------------------------------------------------------

TEST(dag_lib_present_no_sources) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    // lib/ present but with 0 source files
    Crate* c = &f.crates[0];
    strncpy(c->name, "nosrc", sizeof(c->name) - 1);
    strncpy(c->path, "nosrc", sizeof(c->path) - 1);
    c->has_lib = true;
    c->modules[MODULE_LIB].present = true;
    c->modules[MODULE_LIB].kind = MODULE_LIB;
    c->modules[MODULE_LIB].sources.count = 0;
    c->modules[MODULE_LIB].sources.paths = NULL;
    c->module_count = 1;

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(graph != NULL);

    // No compile tasks since no sources
    int compile_count = count_tasks_by_kind(graph, DAG_TASK_COMPILE);
    TEST_ASSERT_EQ(compile_count, 0);

    // Link task still created for lib/ (the module is present)
    int lib_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_LIB);
    TEST_ASSERT(lib_link >= 0);

    // Link task has no compile dependencies
    TEST_ASSERT_EQ(graph->tasks[lib_link].dep_count, 0);

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 10: Non-compilable sources (e.g., .h files) are skipped
// ---------------------------------------------------------------------------

TEST(dag_skips_non_compilable_sources) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    // Mix of compilable and non-compilable files
    const char* sources[] = { "/fake/src/impl.c", "/fake/src/header.h", "/fake/src/data.txt", "/fake/src/other.cpp" };
    dag_fixture_add_lib(&f, 0, "mixed", sources, 4);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);

    // Only .c and .cpp should produce compile tasks (2 out of 4)
    int compile_count = count_tasks_by_kind(graph, DAG_TASK_COMPILE);
    TEST_ASSERT_EQ(compile_count, 2);

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 11: Compile task object_path computation
// ---------------------------------------------------------------------------

TEST(dag_compile_task_object_path_correct) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    const char* sources[] = { "/fake/src/hello.c" };
    dag_fixture_add_lib(&f, 0, "objtest", sources, 1);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);

    // Find the compile task
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == DAG_TASK_COMPILE) {
            // object_path should end with "hello.o"
            const char* obj = graph->tasks[i].object_path;
            size_t len = strlen(obj);
            TEST_ASSERT(len > 7);
            TEST_ASSERT(strcmp(obj + len - 7, "hello.o") == 0);
            break;
        }
    }

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 12: Link task stores compile_task_ids correctly
// ---------------------------------------------------------------------------

TEST(dag_link_task_compile_ids) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    const char* sources[] = { "/fake/src/a.c", "/fake/src/b.c", "/fake/src/c.c" };
    dag_fixture_add_lib(&f, 0, "linktest", sources, 3);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);

    int lib_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_LIB);
    TEST_ASSERT(lib_link >= 0);

    DagTask* link_task = &graph->tasks[lib_link];
    TEST_ASSERT_EQ(link_task->compile_task_count, 3);
    TEST_ASSERT(link_task->compile_task_ids != NULL);

    // Each compile_task_id should be a valid compile task
    for (int i = 0; i < link_task->compile_task_count; i++) {
        int tid = link_task->compile_task_ids[i];
        TEST_ASSERT(tid >= 0);
        TEST_ASSERT(tid < graph->task_count);
        TEST_ASSERT_EQ(graph->tasks[tid].kind, DAG_TASK_COMPILE);
    }

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 13: dag_graph_task_count_by_kind utility
// ---------------------------------------------------------------------------

TEST(dag_task_count_by_kind_utility) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    const char* lib_sources[] = { "/fake/src/x.c" };
    dag_fixture_add_lib(&f, 0, "counttest", lib_sources, 1);
    const char* exe_sources[] = { "/fake/exe/main.c" };
    dag_fixture_add_exe(&f, 0, exe_sources, 1);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_EQ(dag_graph_task_count_by_kind(graph, DAG_TASK_COMPILE), 2);
    TEST_ASSERT_EQ(dag_graph_task_count_by_kind(graph, DAG_TASK_LINK), 2);
    TEST_ASSERT_EQ(dag_graph_task_count_by_kind(graph, DAG_TASK_HOOK_PRE), 0);
    TEST_ASSERT_EQ(dag_graph_task_count_by_kind(graph, DAG_TASK_HOOK_POST), 0);
    TEST_ASSERT_EQ(dag_graph_task_count_by_kind(graph, DAG_TASK_RESOURCE), 0);
    TEST_ASSERT_EQ(dag_graph_task_count_by_kind(graph, DAG_TASK_SHADER), 0);

    // NULL graph returns 0
    TEST_ASSERT_EQ(dag_graph_task_count_by_kind(NULL, DAG_TASK_COMPILE), 0);

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 14: dag_graph_free with NULL is safe
// ---------------------------------------------------------------------------

TEST(dag_graph_free_null_safe) {
    dag_graph_free(NULL); // Should not crash
    return 0;
}

// ---------------------------------------------------------------------------
// Test 15: Multi-crate with exe module depends on upstream lib link
// ---------------------------------------------------------------------------

TEST(dag_multi_crate_exe_depends_upstream_lib_link) {
    DagTestFixture f;
    dag_fixture_init(&f, 2);

    // Crate 0: "base" with lib/
    const char* base_sources[] = { "/fake/base/lib/base.c" };
    dag_fixture_add_lib(&f, 0, "base", base_sources, 1);

    // Crate 1: "app" with lib/ + exe/, depends on base
    const char* app_lib_sources[] = { "/fake/app/lib/app.c" };
    dag_fixture_add_lib(&f, 1, "app", app_lib_sources, 1);
    const char* app_exe_sources[] = { "/fake/app/exe/main.c" };
    dag_fixture_add_exe(&f, 1, app_exe_sources, 1);
    dag_fixture_add_dep(&f, 1, 0);

    f.build_order[0] = 0;
    f.build_order[1] = 1;

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);

    // Find tasks
    int base_lib_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_LIB);
    int app_lib_link = find_task(graph, DAG_TASK_LINK, 1, MODULE_LIB);
    int app_exe_link = find_task(graph, DAG_TASK_LINK, 1, MODULE_EXE);
    TEST_ASSERT(base_lib_link >= 0);
    TEST_ASSERT(app_lib_link >= 0);
    TEST_ASSERT(app_exe_link >= 0);

    // app's exe link should depend on base's lib link (upstream dep)
    TEST_ASSERT(task_depends_on(graph, app_exe_link, base_lib_link));

    // app's exe link should depend on app's lib link (own lib dep)
    TEST_ASSERT(task_depends_on(graph, app_exe_link, app_lib_link));

    // app's lib link should depend on base's lib link
    TEST_ASSERT(task_depends_on(graph, app_lib_link, base_lib_link));

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 16: Finalize marks zero-dep tasks as READY
// ---------------------------------------------------------------------------

TEST(dag_finalize_marks_ready) {
    DagTestFixture f;
    dag_fixture_init(&f, 1);

    const char* sources[] = { "/fake/src/only.c" };
    dag_fixture_add_lib(&f, 0, "readytest", sources, 1);

    DagGraph* graph = NULL;
    int rc = dag_generate(&f.ws, "debug", &graph);
    TEST_ASSERT_EQ(rc, 0);

    // The compile task has no dependencies (no hooks) -> should be READY
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == DAG_TASK_COMPILE) {
            TEST_ASSERT_EQ(graph->tasks[i].status, DAG_STATUS_READY);
            TEST_ASSERT_EQ(graph->tasks[i].remaining_deps, 0);
        }
    }

    // Link task has deps -> should be PENDING
    int lib_link = find_task(graph, DAG_TASK_LINK, 0, MODULE_LIB);
    TEST_ASSERT(lib_link >= 0);
    TEST_ASSERT_EQ(graph->tasks[lib_link].status, DAG_STATUS_PENDING);
    TEST_ASSERT(graph->tasks[lib_link].remaining_deps > 0);

    dag_graph_free(graph);
    dag_fixture_destroy(&f);
    return 0;
}

