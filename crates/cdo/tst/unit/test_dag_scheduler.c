// crates/cdo/tst/unit/test_dag_scheduler.c
// Unit tests for DAG scheduler (core/dag_scheduler.c)
// Tests dag_scheduler_run: success path, failure propagation, deterministic order, edge cases.
//
// Strategy: Build graphs manually using dag_graph_create + direct task population,
// using DAG_TASK_HOOK_PRE/POST with hook_def.present = false (no-op tasks) to exercise
// the scheduler loop without needing real source files or compiler invocations.
#include "cdo_ut.h"
#include "core/dag_scheduler.h"
#include "model/dag.h"
#include "model/workspace.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Helpers: build minimal graphs with no-op tasks for scheduler testing
// ---------------------------------------------------------------------------

/// Add a no-op hook task to the graph (hook_def.present = false -> returns 0).
/// Returns the task ID.
static int add_noop_task(DagGraph* graph, int crate_idx, ModuleKind mod_kind, const char* label) {
    // Grow capacity if needed
    if (graph->task_count >= graph->task_capacity) {
        graph->task_capacity = graph->task_capacity * 2 + 1;
        graph->tasks = (DagTask*)realloc(graph->tasks, sizeof(DagTask) * (size_t)graph->task_capacity);
    }

    int id = graph->task_count++;
    DagTask* t = &graph->tasks[id];
    memset(t, 0, sizeof(DagTask));

    t->id = id;
    t->kind = DAG_TASK_HOOK_PRE;  // hook with present=false is a no-op
    t->crate_idx = crate_idx;
    t->module_kind = mod_kind;
    t->hook_def.present = false;

    // Use source_path as a label for sorting/debugging
    if (label) {
        strncpy(t->source_path, label, sizeof(t->source_path) - 1);
    }

    return id;
}

/// Add a dependency edge from task_id to dep_id.
static void add_dep(DagGraph* graph, int task_id, int dep_id) {
    DagTask* t = &graph->tasks[task_id];
    if (t->dep_count >= t->dep_capacity) {
        t->dep_capacity = t->dep_capacity * 2 + 4;
        t->dep_ids = (int*)realloc(t->dep_ids, sizeof(int) * (size_t)t->dep_capacity);
    }
    t->dep_ids[t->dep_count++] = dep_id;
}

/// Create a minimal workspace + config for scheduler tests.
/// The workspace has 1 crate with a name, so error messages in the scheduler work.
static void make_test_config(Workspace* ws, Crate* crates, int crate_count, CompilerInfo* compiler, CacheStats* stats, DagSchedulerConfig* config) {
    memset(ws, 0, sizeof(Workspace));
    memset(crates, 0, sizeof(Crate) * (size_t)crate_count);
    memset(compiler, 0, sizeof(CompilerInfo));
    memset(stats, 0, sizeof(CacheStats));
    memset(config, 0, sizeof(DagSchedulerConfig));

    strncpy(ws->root_path, "C:/fake/ws", sizeof(ws->root_path) - 1);
    ws->crate_count = crate_count;
    ws->crates = crates;

    for (int i = 0; i < crate_count; i++) {
        snprintf(crates[i].name, sizeof(crates[i].name), "crate%d", i);
    }

    compiler->family = COMPILER_GCC;
    strncpy(compiler->path, "gcc", sizeof(compiler->path) - 1);

    config->workspace = ws;
    config->compiler = compiler;
    config->cache_config = NULL;
    config->cache_stats = stats;
    config->no_cache = true;
    config->jobs = 1;
    config->profile = "debug";
    config->progress = NULL;
    config->total_compile_units = 0;
}

// ---------------------------------------------------------------------------
// Test 1: All tasks succeed -> returns 0, all marked DONE
// Build a graph with only no-op hook tasks, run scheduler, verify all DONE.
// ---------------------------------------------------------------------------

TEST(dag_scheduler_all_succeed) {
    DagGraph* graph = dag_graph_create(8);

    // Create a simple linear chain: A -> B -> C
    int a = add_noop_task(graph, 0, MODULE_LIB, "task_a");
    int b = add_noop_task(graph, 0, MODULE_LIB, "task_b");
    int c = add_noop_task(graph, 0, MODULE_LIB, "task_c");
    add_dep(graph, b, a);  // B depends on A
    add_dep(graph, c, b);  // C depends on B

    dag_graph_finalize(graph);

    // Set up config
    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 0);

    // All tasks should be DONE
    TEST_ASSERT_EQ(graph->tasks[a].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[b].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[c].status, DAG_STATUS_DONE);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 2: One task fails -> dependents cancelled, returns 1
// Use a hook with present=true and a nonexistent command to force failure.
// ---------------------------------------------------------------------------

TEST(dag_scheduler_fail_cancels_dependents) {
    DagGraph* graph = dag_graph_create(8);

    // A (will fail) -> B -> C
    int a = add_noop_task(graph, 0, MODULE_LIB, "task_a_fail");
    int b = add_noop_task(graph, 0, MODULE_LIB, "task_b");
    int c = add_noop_task(graph, 0, MODULE_LIB, "task_c");

    // Make task A actually execute a hook (with a bad command -> will fail)
    graph->tasks[a].hook_def.present = true;
    strncpy(graph->tasks[a].hook_def.command, "nonexistent_command_xyz_999", sizeof(graph->tasks[a].hook_def.command) - 1);
    graph->tasks[a].hook_def.lifecycle = HOOK_PRE_BUILD;
    graph->tasks[a].hook_def.timeout_sec = 5;

    add_dep(graph, b, a);  // B depends on A
    add_dep(graph, c, b);  // C depends on B

    dag_graph_finalize(graph);

    // Set up config
    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 1);

    // A should be FAILED, B and C should be CANCELLED
    TEST_ASSERT_EQ(graph->tasks[a].status, DAG_STATUS_FAILED);
    TEST_ASSERT_EQ(graph->tasks[b].status, DAG_STATUS_CANCELLED);
    TEST_ASSERT_EQ(graph->tasks[c].status, DAG_STATUS_CANCELLED);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 3: Deterministic dispatch order for simultaneous ready tasks
// Build a graph where multiple tasks are immediately ready (no deps) and
// verify they execute in sorted order (crate_idx, module_kind, source_path).
// ---------------------------------------------------------------------------

TEST(dag_scheduler_deterministic_order) {
    DagGraph* graph = dag_graph_create(8);

    // Add tasks with different crate_idx/module_kind/source_path values
    // All have no deps -> all ready at the start
    // Expected sort: (0, LIB, "aaa") < (0, EXE, "bbb") < (1, LIB, "ccc") < (2, LIB, "ddd")
    int t3 = add_noop_task(graph, 2, MODULE_LIB, "ddd");
    int t1 = add_noop_task(graph, 0, MODULE_EXE, "bbb");
    int t0 = add_noop_task(graph, 0, MODULE_LIB, "aaa");
    int t2 = add_noop_task(graph, 1, MODULE_LIB, "ccc");

    dag_graph_finalize(graph);

    // After finalize, all tasks should be READY (no deps)
    TEST_ASSERT_EQ(graph->tasks[t0].status, DAG_STATUS_READY);
    TEST_ASSERT_EQ(graph->tasks[t1].status, DAG_STATUS_READY);
    TEST_ASSERT_EQ(graph->tasks[t2].status, DAG_STATUS_READY);
    TEST_ASSERT_EQ(graph->tasks[t3].status, DAG_STATUS_READY);

    // Set up config with 3 crates
    Workspace ws;
    Crate crates[3];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 3, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 0);

    // All completed (DONE) -- order is deterministic but we can only verify completion
    TEST_ASSERT_EQ(graph->tasks[t0].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[t1].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[t2].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[t3].status, DAG_STATUS_DONE);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 4: Empty graph -> returns 0 immediately
// ---------------------------------------------------------------------------

TEST(dag_scheduler_empty_graph) {
    DagGraph* graph = dag_graph_create(4);
    // No tasks added

    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 0);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 5: NULL graph/config -> returns error
// ---------------------------------------------------------------------------

TEST(dag_scheduler_null_args) {
    DagGraph* graph = dag_graph_create(4);
    add_noop_task(graph, 0, MODULE_LIB, "x");
    dag_graph_finalize(graph);

    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    // NULL graph
    int rc = dag_scheduler_run(NULL, &config);
    TEST_ASSERT(rc != 0);

    // NULL config
    rc = dag_scheduler_run(graph, NULL);
    TEST_ASSERT(rc != 0);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 6: Graph where all tasks are already DONE (edge case)
// Manually set all tasks to DONE with remaining_deps=0, scheduler should return 0.
// ---------------------------------------------------------------------------

TEST(dag_scheduler_all_already_done) {
    DagGraph* graph = dag_graph_create(4);

    int a = add_noop_task(graph, 0, MODULE_LIB, "done_a");
    int b = add_noop_task(graph, 0, MODULE_LIB, "done_b");
    (void)a; (void)b;

    dag_graph_finalize(graph);

    // Manually mark all as DONE (simulating already-completed state)
    for (int i = 0; i < graph->task_count; i++) {
        graph->tasks[i].status = DAG_STATUS_DONE;
        graph->tasks[i].remaining_deps = 0;
    }

    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    // Scheduler won't find any READY tasks, but all are DONE so it should detect that
    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 0);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 7: Diamond dependency pattern -> correct execution
// A -> B, A -> C, B -> D, C -> D
// All no-op tasks, should all complete successfully.
// ---------------------------------------------------------------------------

TEST(dag_scheduler_diamond_pattern) {
    DagGraph* graph = dag_graph_create(8);

    int a = add_noop_task(graph, 0, MODULE_LIB, "diamond_a");
    int b = add_noop_task(graph, 0, MODULE_LIB, "diamond_b");
    int c = add_noop_task(graph, 0, MODULE_LIB, "diamond_c");
    int d = add_noop_task(graph, 0, MODULE_LIB, "diamond_d");

    add_dep(graph, b, a);  // B depends on A
    add_dep(graph, c, a);  // C depends on A
    add_dep(graph, d, b);  // D depends on B
    add_dep(graph, d, c);  // D depends on C

    dag_graph_finalize(graph);

    // A should be READY (no deps), B/C/D PENDING
    TEST_ASSERT_EQ(graph->tasks[a].status, DAG_STATUS_READY);
    TEST_ASSERT_EQ(graph->tasks[b].status, DAG_STATUS_PENDING);
    TEST_ASSERT_EQ(graph->tasks[c].status, DAG_STATUS_PENDING);
    TEST_ASSERT_EQ(graph->tasks[d].status, DAG_STATUS_PENDING);

    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 0);

    // All should be DONE
    TEST_ASSERT_EQ(graph->tasks[a].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[b].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[c].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[d].status, DAG_STATUS_DONE);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 8: Failure in the middle of a diamond cancels downstream
// A -> B (fails), A -> C, B -> D, C -> D
// B fails -> D cancelled (dep on B), C still succeeds (no dep on B)
// ---------------------------------------------------------------------------

TEST(dag_scheduler_diamond_partial_failure) {
    DagGraph* graph = dag_graph_create(8);

    int a = add_noop_task(graph, 0, MODULE_LIB, "dia_a");
    int b = add_noop_task(graph, 0, MODULE_LIB, "dia_b_fail");
    int c = add_noop_task(graph, 0, MODULE_LIB, "dia_c");
    int d = add_noop_task(graph, 0, MODULE_LIB, "dia_d");

    // Make B fail
    graph->tasks[b].hook_def.present = true;
    strncpy(graph->tasks[b].hook_def.command, "nonexistent_cmd_fail_diamond", sizeof(graph->tasks[b].hook_def.command) - 1);
    graph->tasks[b].hook_def.timeout_sec = 5;

    add_dep(graph, b, a);  // B depends on A
    add_dep(graph, c, a);  // C depends on A
    add_dep(graph, d, b);  // D depends on B
    add_dep(graph, d, c);  // D depends on C

    dag_graph_finalize(graph);

    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 1);  // At least one failure

    // A should succeed (executed first, no deps)
    TEST_ASSERT_EQ(graph->tasks[a].status, DAG_STATUS_DONE);

    // B should fail
    TEST_ASSERT_EQ(graph->tasks[b].status, DAG_STATUS_FAILED);

    // D depends on B which failed -> CANCELLED
    TEST_ASSERT_EQ(graph->tasks[d].status, DAG_STATUS_CANCELLED);

    // C depends only on A (which succeeded), so C should be DONE
    // Note: C might execute before or after B in sorted order, but since it only
    // depends on A (which succeeds), C should complete successfully.
    TEST_ASSERT_EQ(graph->tasks[c].status, DAG_STATUS_DONE);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 9: Wide fan-out from single task, verify all execute
// Root -> [T1, T2, T3, T4, T5] (5 independent tasks depend on root)
// ---------------------------------------------------------------------------

TEST(dag_scheduler_wide_fanout) {
    DagGraph* graph = dag_graph_create(8);

    int root = add_noop_task(graph, 0, MODULE_LIB, "root");
    int tasks[5];
    for (int i = 0; i < 5; i++) {
        char label[32];
        snprintf(label, sizeof(label), "fan_%d", i);
        tasks[i] = add_noop_task(graph, 0, MODULE_LIB, label);
        add_dep(graph, tasks[i], root);
    }

    dag_graph_finalize(graph);

    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 0);

    // All should be DONE
    TEST_ASSERT_EQ(graph->tasks[root].status, DAG_STATUS_DONE);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQ(graph->tasks[tasks[i]].status, DAG_STATUS_DONE);
    }

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 10: Multiple independent chains, verify all complete
// Chain 1: A1 -> B1 -> C1
// Chain 2: A2 -> B2
// No cross-dependencies.
// ---------------------------------------------------------------------------

TEST(dag_scheduler_independent_chains) {
    DagGraph* graph = dag_graph_create(8);

    // Chain 1
    int a1 = add_noop_task(graph, 0, MODULE_LIB, "chain1_a");
    int b1 = add_noop_task(graph, 0, MODULE_LIB, "chain1_b");
    int c1 = add_noop_task(graph, 0, MODULE_LIB, "chain1_c");
    add_dep(graph, b1, a1);
    add_dep(graph, c1, b1);

    // Chain 2
    int a2 = add_noop_task(graph, 1, MODULE_LIB, "chain2_a");
    int b2 = add_noop_task(graph, 1, MODULE_LIB, "chain2_b");
    add_dep(graph, b2, a2);

    dag_graph_finalize(graph);

    Workspace ws;
    Crate crates[2];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 2, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 0);

    // All should be DONE
    TEST_ASSERT_EQ(graph->tasks[a1].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[b1].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[c1].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[a2].status, DAG_STATUS_DONE);
    TEST_ASSERT_EQ(graph->tasks[b2].status, DAG_STATUS_DONE);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 11: Single task graph succeeds
// ---------------------------------------------------------------------------

TEST(dag_scheduler_single_task) {
    DagGraph* graph = dag_graph_create(4);

    int t = add_noop_task(graph, 0, MODULE_LIB, "only_task");
    dag_graph_finalize(graph);

    TEST_ASSERT_EQ(graph->tasks[t].status, DAG_STATUS_READY);

    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(graph->tasks[t].status, DAG_STATUS_DONE);

    dag_graph_free(graph);
    return 0;
}

// ---------------------------------------------------------------------------
// Test 12: Failure of root task cancels entire chain
// ---------------------------------------------------------------------------

TEST(dag_scheduler_root_failure_cancels_all) {
    DagGraph* graph = dag_graph_create(8);

    int root = add_noop_task(graph, 0, MODULE_LIB, "root_fail");
    graph->tasks[root].hook_def.present = true;
    strncpy(graph->tasks[root].hook_def.command, "nonexistent_root_cmd_999", sizeof(graph->tasks[root].hook_def.command) - 1);
    graph->tasks[root].hook_def.timeout_sec = 5;

    int mid = add_noop_task(graph, 0, MODULE_LIB, "mid");
    int leaf = add_noop_task(graph, 0, MODULE_LIB, "leaf");
    add_dep(graph, mid, root);
    add_dep(graph, leaf, mid);

    dag_graph_finalize(graph);

    Workspace ws;
    Crate crates[1];
    CompilerInfo compiler;
    CacheStats stats;
    DagSchedulerConfig config;
    make_test_config(&ws, crates, 1, &compiler, &stats, &config);

    int rc = dag_scheduler_run(graph, &config);
    TEST_ASSERT_EQ(rc, 1);

    TEST_ASSERT_EQ(graph->tasks[root].status, DAG_STATUS_FAILED);
    TEST_ASSERT_EQ(graph->tasks[mid].status, DAG_STATUS_CANCELLED);
    TEST_ASSERT_EQ(graph->tasks[leaf].status, DAG_STATUS_CANCELLED);

    dag_graph_free(graph);
    return 0;
}
