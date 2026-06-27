/*
 * pbt_threadpool.c - Property: Thread Pool Task Completion
 * Validates: Requirements 6.1
 *
 * For any set of N independent tasks submitted to the thread pool, after
 * waiting for completion, exactly N tasks SHALL have executed and their
 * results SHALL be available.
 */
#include "cdo_ut.h"
#include "vendor/theft.h"
#include "commons/threadpool.h"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #define ATOMIC_INCREMENT(p) InterlockedIncrement((volatile LONG*)(p))
#else
  #define ATOMIC_INCREMENT(p) __sync_fetch_and_add((p), 1)
#endif

typedef struct {
    volatile long *counter;
    int           *flags;
    int            task_idx;
} TaskCompletionArg;

static void task_completion_func(void *arg) {
    TaskCompletionArg *ctx = (TaskCompletionArg *)arg;
    ATOMIC_INCREMENT(ctx->counter);
    ATOMIC_INCREMENT(&ctx->flags[ctx->task_idx]);
}

static enum theft_alloc_res
alloc_task_count(struct theft *t, void *env, void **output) {
    (void)env;
    int *n = malloc(sizeof(int));
    if (!n) return THEFT_ALLOC_ERROR;
    *n = (int)(theft_random_choice(t, 500) + 1);
    *output = n;
    return THEFT_ALLOC_OK;
}

static void free_task_count(void *instance, void *env) { (void)env; free(instance); }

static void print_task_count(FILE *f, const void *instance, void *env) {
    (void)env;
    fprintf(f, "N=%d", *(const int *)instance);
}

static struct theft_type_info task_count_type_info = {
    .alloc  = alloc_task_count,
    .free   = free_task_count,
    .print  = print_task_count,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static enum theft_trial_res
prop_threadpool_task_completion(struct theft *t, void *arg1) {
    (void)t;
    int n = *(int *)arg1;

    ThreadPool *pool = threadpool_create(4);
    if (!pool) return THEFT_TRIAL_ERROR;

    volatile long counter = 0;
    int *flags = (int *)calloc((size_t)n, sizeof(int));
    if (!flags) { threadpool_destroy(pool); return THEFT_TRIAL_ERROR; }

    TaskCompletionArg *args = (TaskCompletionArg *)malloc((size_t)n * sizeof(TaskCompletionArg));
    if (!args) { free(flags); threadpool_destroy(pool); return THEFT_TRIAL_ERROR; }

    for (int i = 0; i < n; i++) {
        args[i].counter  = &counter;
        args[i].flags    = flags;
        args[i].task_idx = i;
        int rc = threadpool_submit(pool, task_completion_func, &args[i]);
        if (rc != 0) {
            threadpool_wait(pool); threadpool_destroy(pool);
            free(args); free(flags);
            return THEFT_TRIAL_ERROR;
        }
    }

    threadpool_wait(pool);

    enum theft_trial_res result = THEFT_TRIAL_PASS;

    if ((long)counter != (long)n) {
        fprintf(stderr, "  FAIL: counter=%ld, expected=%d\n", (long)counter, n);
        result = THEFT_TRIAL_FAIL;
    }

    if (result == THEFT_TRIAL_PASS) {
        for (int i = 0; i < n; i++) {
            if (flags[i] == 0) {
                fprintf(stderr, "  FAIL: task %d was never executed\n", i);
                result = THEFT_TRIAL_FAIL; break;
            }
            if (flags[i] > 1) {
                fprintf(stderr, "  FAIL: task %d was executed %d times\n", i, flags[i]);
                result = THEFT_TRIAL_FAIL; break;
            }
        }
    }

    threadpool_destroy(pool);
    free(args);
    free(flags);
    return result;
}

TEST(prop_threadpool_task_completion) {
    struct theft_run_config cfg = {
        .name = "threadpool_task_completion",
        .prop = { .prop1 = prop_threadpool_task_completion },
        .type_info = { &task_count_type_info },
        .seed = 60601,
        .trials = 100,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}
