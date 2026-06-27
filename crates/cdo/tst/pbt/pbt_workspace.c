/*
 * pbt_workspace.c - Workspace Property-Based Tests
 *
 * Property 3: Topological Sort Ordering (Requirements 2.3)
 * Property 4: Circular Dependency Detection (Requirements 2.5)
 * Property 5: Transitive Dependency Closure (Requirements 4.2)
 */
#include "cdo_ut.h"
#include "vendor/theft.h"
#include "core/workspace.h"

#ifndef CDO_ERR_CYCLE
#define CDO_ERR_CYCLE 8
#endif

/*============================================================================
 * Property 3: Topological Sort Ordering
 *============================================================================*/

typedef struct {
    Workspace ws;
    int       edge_count;
} TopoSortDag;

static enum theft_alloc_res
alloc_topo_sort_dag(struct theft *t, void *env, void **output) {
    (void)env;

    TopoSortDag *dw = calloc(1, sizeof(TopoSortDag));
    if (!dw) return THEFT_ALLOC_ERROR;

    int n = (int)(theft_random_choice(t, 7) + 2);

    memset(&dw->ws, 0, sizeof(Workspace));
    dw->ws.crate_count = n;
    dw->ws.crates = calloc((size_t)n, sizeof(Crate));
    if (!dw->ws.crates) { free(dw); return THEFT_ALLOC_ERROR; }

    for (int i = 0; i < n; i++) {
        snprintf(dw->ws.crates[i].name, sizeof(dw->ws.crates[i].name), "crate_%d", i);
        snprintf(dw->ws.crates[i].path, sizeof(dw->ws.crates[i].path), "crates/crate_%d", i);
        dw->ws.crates[i].type = CRATE_STATIC_LIB;
        dw->ws.crates[i].c_standard = 17;
        dw->ws.crates[i].dep_count = 0;
        dw->ws.crates[i].dep_indices = NULL;
    }

    dw->edge_count = 0;
    for (int i = 1; i < n; i++) {
        int max_deps = i < 3 ? i : 3;
        int dep_count = (int)theft_random_choice(t, (uint64_t)(max_deps + 1));
        if (dep_count == 0) continue;

        dw->ws.crates[i].dep_indices = calloc((size_t)dep_count, sizeof(int));
        if (!dw->ws.crates[i].dep_indices) {
            for (int j = 0; j < i; j++) free(dw->ws.crates[j].dep_indices);
            free(dw->ws.crates); free(dw);
            return THEFT_ALLOC_ERROR;
        }

        int added = 0;
        bool *used = calloc((size_t)i, sizeof(bool));
        if (!used) {
            free(dw->ws.crates[i].dep_indices);
            for (int j = 0; j < i; j++) free(dw->ws.crates[j].dep_indices);
            free(dw->ws.crates); free(dw);
            return THEFT_ALLOC_ERROR;
        }

        for (int d = 0; d < dep_count; d++) {
            int attempts = 0;
            int target;
            do {
                target = (int)theft_random_choice(t, (uint64_t)i);
                attempts++;
            } while (used[target] && attempts < i * 2);

            if (!used[target]) {
                used[target] = true;
                dw->ws.crates[i].dep_indices[added] = target;
                added++;
            }
        }
        free(used);
        dw->ws.crates[i].dep_count = added;
        dw->edge_count += added;
    }

    if (dw->edge_count == 0) {
        dw->ws.crates[1].dep_indices = calloc(1, sizeof(int));
        if (!dw->ws.crates[1].dep_indices) { free(dw->ws.crates); free(dw); return THEFT_ALLOC_ERROR; }
        dw->ws.crates[1].dep_indices[0] = 0;
        dw->ws.crates[1].dep_count = 1;
        dw->edge_count = 1;
    }

    *output = dw;
    return THEFT_ALLOC_OK;
}

static void free_topo_sort_dag(void *instance, void *env) {
    (void)env;
    TopoSortDag *dw = (TopoSortDag *)instance;
    if (!dw) return;
    for (int i = 0; i < dw->ws.crate_count; i++) free(dw->ws.crates[i].dep_indices);
    free(dw->ws.crates);
    free(dw->ws.build_order);
    free(dw);
}

static void print_topo_sort_dag(FILE *f, const void *instance, void *env) {
    (void)env;
    const TopoSortDag *dw = (const TopoSortDag *)instance;
    fprintf(f, "TopoSortDag(crates=%d, edges=%d)", dw->ws.crate_count, dw->edge_count);
}

static struct theft_type_info topo_sort_dag_type_info = {
    .alloc  = alloc_topo_sort_dag,
    .free   = free_topo_sort_dag,
    .print  = print_topo_sort_dag,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static enum theft_trial_res
prop_topological_sort_ordering(struct theft *t, void *arg1) {
    (void)t;
    TopoSortDag *dw = (TopoSortDag *)arg1;

    int rc = workspace_resolve(&dw->ws, NULL, 0);
    if (rc != 0) {
        fprintf(stderr, "  workspace_resolve returned %d (unexpected for DAG)\n", rc);
        return THEFT_TRIAL_FAIL;
    }

    if (dw->ws.build_order_count != dw->ws.crate_count) {
        fprintf(stderr, "  build_order_count=%d != crate_count=%d\n",
                dw->ws.build_order_count, dw->ws.crate_count);
        return THEFT_TRIAL_FAIL;
    }

    int n = dw->ws.crate_count;
    int *position = calloc((size_t)n, sizeof(int));
    if (!position) return THEFT_TRIAL_ERROR;

    for (int pos = 0; pos < dw->ws.build_order_count; pos++) {
        int crate_idx = dw->ws.build_order[pos];
        if (crate_idx < 0 || crate_idx >= n) {
            free(position); return THEFT_TRIAL_FAIL;
        }
        position[crate_idx] = pos;
    }

    for (int i = 0; i < n; i++) {
        const Crate *crate = &dw->ws.crates[i];
        for (int d = 0; d < crate->dep_count; d++) {
            int j = crate->dep_indices[d];
            if (j < 0 || j >= n) continue;
            if (position[j] >= position[i]) {
                fprintf(stderr, "  FAIL: %s (pos %d) depends on %s (pos %d)\n",
                        crate->name, position[i], dw->ws.crates[j].name, position[j]);
                free(position);
                return THEFT_TRIAL_FAIL;
            }
        }
    }

    free(position);
    return THEFT_TRIAL_PASS;
}

TEST(prop_topological_sort_ordering) {
    struct theft_run_config cfg = {
        .name = "topological_sort_ordering",
        .prop = { .prop1 = prop_topological_sort_ordering },
        .type_info = { &topo_sort_dag_type_info },
        .seed = 30303,
        .trials = 500,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}

/*============================================================================
 * Property 4: Circular Dependency Detection
 *============================================================================*/

typedef struct {
    Workspace ws;
} CyclicWorkspace;

static enum theft_alloc_res
alloc_cyclic_workspace(struct theft *t, void *env, void **output) {
    (void)env;

    CyclicWorkspace *cw = calloc(1, sizeof(CyclicWorkspace));
    if (!cw) return THEFT_ALLOC_ERROR;

    memset(&cw->ws, 0, sizeof(Workspace));
    strncpy(cw->ws.root_path, "/tmp/test_ws", sizeof(cw->ws.root_path) - 1);

    int n = (int)(theft_random_choice(t, 6) + 3);
    cw->ws.crate_count = n;
    cw->ws.crates = calloc((size_t)n, sizeof(Crate));
    if (!cw->ws.crates) { free(cw); return THEFT_ALLOC_ERROR; }

    for (int i = 0; i < n; i++) {
        snprintf(cw->ws.crates[i].name, sizeof(cw->ws.crates[i].name), "crate%d", i);
        snprintf(cw->ws.crates[i].path, sizeof(cw->ws.crates[i].path), "crates/crate%d", i);
        cw->ws.crates[i].type = CRATE_STATIC_LIB;
    }

    int cycle_len = (int)(theft_random_choice(t, (uint64_t)(n - 1)) + 2);

    int *perm = malloc((size_t)n * sizeof(int));
    if (!perm) { free(cw->ws.crates); free(cw); return THEFT_ALLOC_ERROR; }
    for (int i = 0; i < n; i++) perm[i] = i;
    for (int i = n - 1; i > 0; i--) {
        int j = (int)theft_random_choice(t, (uint64_t)(i + 1));
        int tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
    }

    typedef struct { int from; int to; } Edge;
    int max_edges = n * n;
    Edge *edges = calloc((size_t)max_edges, sizeof(Edge));
    if (!edges) { free(perm); free(cw->ws.crates); free(cw); return THEFT_ALLOC_ERROR; }
    int edge_count = 0;

    for (int i = 0; i < cycle_len; i++) {
        edges[edge_count].from = perm[i];
        edges[edge_count].to = perm[(i + 1) % cycle_len];
        edge_count++;
    }

    int extra_edges = (int)theft_random_choice(t, (uint64_t)(n + 1));
    for (int e = 0; e < extra_edges; e++) {
        int from = (int)theft_random_choice(t, (uint64_t)n);
        int to = (int)theft_random_choice(t, (uint64_t)n);
        if (from == to) continue;
        bool dup = false;
        for (int k = 0; k < edge_count; k++) {
            if (edges[k].from == from && edges[k].to == to) { dup = true; break; }
        }
        if (!dup && edge_count < max_edges) {
            edges[edge_count].from = from;
            edges[edge_count].to = to;
            edge_count++;
        }
    }

    for (int i = 0; i < n; i++) {
        int dep_count = 0;
        for (int e = 0; e < edge_count; e++) {
            if (edges[e].from == i) dep_count++;
        }
        cw->ws.crates[i].dep_count = dep_count;
        if (dep_count > 0) {
            cw->ws.crates[i].dep_indices = malloc((size_t)dep_count * sizeof(int));
            if (!cw->ws.crates[i].dep_indices) {
                for (int j = 0; j < i; j++) free(cw->ws.crates[j].dep_indices);
                free(edges); free(perm); free(cw->ws.crates); free(cw);
                return THEFT_ALLOC_ERROR;
            }
            int di = 0;
            for (int e = 0; e < edge_count; e++) {
                if (edges[e].from == i) cw->ws.crates[i].dep_indices[di++] = edges[e].to;
            }
        } else {
            cw->ws.crates[i].dep_indices = NULL;
        }
    }

    free(edges);
    free(perm);
    *output = cw;
    return THEFT_ALLOC_OK;
}

static void free_cyclic_workspace(void *instance, void *env) {
    (void)env;
    CyclicWorkspace *cw = (CyclicWorkspace *)instance;
    if (!cw) return;
    for (int i = 0; i < cw->ws.crate_count; i++) free(cw->ws.crates[i].dep_indices);
    free(cw->ws.crates);
    free(cw->ws.build_order);
    free(cw);
}

static void print_cyclic_workspace(FILE *f, const void *instance, void *env) {
    (void)env;
    const CyclicWorkspace *cw = (const CyclicWorkspace *)instance;
    fprintf(f, "CyclicWorkspace(crates=%d)", cw->ws.crate_count);
}

static struct theft_type_info cyclic_workspace_type_info = {
    .alloc  = alloc_cyclic_workspace,
    .free   = free_cyclic_workspace,
    .print  = print_cyclic_workspace,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static enum theft_trial_res
prop_circular_dependency_detection(struct theft *t, void *arg1) {
    (void)t;
    CyclicWorkspace *cw = (CyclicWorkspace *)arg1;

    int result = workspace_resolve(&cw->ws, NULL, 0);

    if (result != CDO_ERR_CYCLE) {
        fprintf(stderr, "  FAIL: expected CDO_ERR_CYCLE (%d), got %d\n", CDO_ERR_CYCLE, result);
        return THEFT_TRIAL_FAIL;
    }
    if (cw->ws.build_order != NULL) {
        fprintf(stderr, "  FAIL: build_order should be NULL after cycle detection\n");
        return THEFT_TRIAL_FAIL;
    }
    if (cw->ws.build_order_count != 0) {
        fprintf(stderr, "  FAIL: build_order_count should be 0, got %d\n", cw->ws.build_order_count);
        return THEFT_TRIAL_FAIL;
    }

    return THEFT_TRIAL_PASS;
}

TEST(prop_circular_dependency_detection) {
    struct theft_run_config cfg = {
        .name = "circular_dependency_detection",
        .prop = { .prop1 = prop_circular_dependency_detection },
        .type_info = { &cyclic_workspace_type_info },
        .seed = 44444,
        .trials = 500,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}

/*============================================================================
 * Property 5: Transitive Dependency Closure
 *============================================================================*/

typedef struct {
    Workspace ws;
    int       selected_count;
    int*      selected_indices;
} TransClosureInput;

static enum theft_alloc_res
alloc_trans_closure_input(struct theft *t, void *env, void **output) {
    (void)env;

    TransClosureInput *tc = calloc(1, sizeof(TransClosureInput));
    if (!tc) return THEFT_ALLOC_ERROR;

    memset(&tc->ws, 0, sizeof(Workspace));
    strncpy(tc->ws.root_path, "/tmp/test_ws", sizeof(tc->ws.root_path) - 1);

    int n = (int)(theft_random_choice(t, 11) + 2);
    tc->ws.crate_count = n;
    tc->ws.crates = calloc((size_t)n, sizeof(Crate));
    if (!tc->ws.crates) { free(tc); return THEFT_ALLOC_ERROR; }

    for (int i = 0; i < n; i++) {
        snprintf(tc->ws.crates[i].name, sizeof(tc->ws.crates[i].name), "tc%d", i);
        snprintf(tc->ws.crates[i].path, sizeof(tc->ws.crates[i].path), "crates/tc%d", i);
        tc->ws.crates[i].type = CRATE_STATIC_LIB;
    }

    for (int i = 1; i < n; i++) {
        int dep_count = 0;
        int *temp_deps = malloc((size_t)i * sizeof(int));
        if (!temp_deps) {
            for (int k = 0; k < i; k++) free(tc->ws.crates[k].dep_indices);
            free(tc->ws.crates); free(tc);
            return THEFT_ALLOC_ERROR;
        }
        for (int j = 0; j < i; j++) {
            if (theft_random_choice(t, 10) < 3) temp_deps[dep_count++] = j;
        }
        if (dep_count > 0) {
            tc->ws.crates[i].dep_indices = malloc((size_t)dep_count * sizeof(int));
            if (!tc->ws.crates[i].dep_indices) {
                free(temp_deps);
                for (int k = 0; k < i; k++) free(tc->ws.crates[k].dep_indices);
                free(tc->ws.crates); free(tc);
                return THEFT_ALLOC_ERROR;
            }
            memcpy(tc->ws.crates[i].dep_indices, temp_deps, (size_t)dep_count * sizeof(int));
            tc->ws.crates[i].dep_count = dep_count;
        }
        free(temp_deps);
    }

    int sel_count = (int)(theft_random_choice(t, (uint64_t)n) + 1);
    int *indices = malloc((size_t)n * sizeof(int));
    if (!indices) {
        for (int k = 0; k < n; k++) free(tc->ws.crates[k].dep_indices);
        free(tc->ws.crates); free(tc);
        return THEFT_ALLOC_ERROR;
    }
    for (int i = 0; i < n; i++) indices[i] = i;
    for (int i = 0; i < sel_count; i++) {
        int j = i + (int)theft_random_choice(t, (uint64_t)(n - i));
        int tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp;
    }

    tc->selected_indices = malloc((size_t)sel_count * sizeof(int));
    if (!tc->selected_indices) {
        free(indices);
        for (int k = 0; k < n; k++) free(tc->ws.crates[k].dep_indices);
        free(tc->ws.crates); free(tc);
        return THEFT_ALLOC_ERROR;
    }
    memcpy(tc->selected_indices, indices, (size_t)sel_count * sizeof(int));
    tc->selected_count = sel_count;
    free(indices);

    tc->ws.build_order = NULL;
    tc->ws.build_order_count = 0;

    *output = tc;
    return THEFT_ALLOC_OK;
}

static void free_trans_closure_input(void *instance, void *env) {
    (void)env;
    TransClosureInput *tc = (TransClosureInput *)instance;
    if (!tc) return;
    for (int i = 0; i < tc->ws.crate_count; i++) free(tc->ws.crates[i].dep_indices);
    free(tc->ws.crates);
    free(tc->ws.build_order);
    free(tc->selected_indices);
    free(tc);
}

static void print_trans_closure_input(FILE *f, const void *instance, void *env) {
    (void)env;
    const TransClosureInput *tc = (const TransClosureInput *)instance;
    fprintf(f, "TransClosureInput(crates=%d, selected=%d)", tc->ws.crate_count, tc->selected_count);
}

static struct theft_type_info trans_closure_type_info = {
    .alloc  = alloc_trans_closure_input,
    .free   = free_trans_closure_input,
    .print  = print_trans_closure_input,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static void compute_expected_closure(const Workspace *ws, const int *selected,
                                     int selected_count, bool *expected) {
    for (int i = 0; i < selected_count; i++) expected[selected[i]] = true;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < ws->crate_count; i++) {
            if (!expected[i]) continue;
            const Crate *crate = &ws->crates[i];
            for (int d = 0; d < crate->dep_count; d++) {
                int dep = crate->dep_indices[d];
                if (dep >= 0 && dep < ws->crate_count && !expected[dep]) {
                    expected[dep] = true;
                    changed = true;
                }
            }
        }
    }
}

static enum theft_trial_res
prop_transitive_dep_closure(struct theft *t, void *arg1) {
    (void)t;
    TransClosureInput *tc = (TransClosureInput *)arg1;

    const char **crate_names = malloc((size_t)tc->selected_count * sizeof(const char *));
    if (!crate_names) return THEFT_TRIAL_ERROR;
    for (int i = 0; i < tc->selected_count; i++) {
        crate_names[i] = tc->ws.crates[tc->selected_indices[i]].name;
    }

    int rc = workspace_resolve(&tc->ws, crate_names, tc->selected_count);
    free(crate_names);
    if (rc != 0) {
        fprintf(stderr, "  ERROR: workspace_resolve returned %d for acyclic DAG\n", rc);
        return THEFT_TRIAL_FAIL;
    }

    bool *expected = calloc((size_t)tc->ws.crate_count, sizeof(bool));
    if (!expected) return THEFT_TRIAL_ERROR;
    compute_expected_closure(&tc->ws, tc->selected_indices, tc->selected_count, expected);

    for (int i = 0; i < tc->ws.crate_count; i++) {
        if (!expected[i]) continue;
        bool found = false;
        for (int j = 0; j < tc->ws.build_order_count; j++) {
            if (tc->ws.build_order[j] == i) { found = true; break; }
        }
        if (!found) {
            fprintf(stderr, "  FAIL: crate '%s' expected but NOT in build_order\n",
                    tc->ws.crates[i].name);
            free(expected);
            return THEFT_TRIAL_FAIL;
        }
    }

    for (int j = 0; j < tc->ws.build_order_count; j++) {
        int idx = tc->ws.build_order[j];
        if (!expected[idx]) {
            fprintf(stderr, "  FAIL: crate '%s' in build_order but NOT expected\n",
                    tc->ws.crates[idx].name);
            free(expected);
            return THEFT_TRIAL_FAIL;
        }
    }

    free(expected);
    return THEFT_TRIAL_PASS;
}

TEST(prop_transitive_dep_closure) {
    struct theft_run_config cfg = {
        .name = "transitive_dependency_closure",
        .prop = { .prop1 = prop_transitive_dep_closure },
        .type_info = { &trans_closure_type_info },
        .seed = 42424,
        .trials = 500,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}
