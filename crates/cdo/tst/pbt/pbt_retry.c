/*
 * pbt_retry.c - Property: Retry Logic Correctness
 * Validates: Requirements 25.2
 *
 * For any sequence of download attempts where the first K attempts fail and
 * attempt K+1 succeeds (K <= 3), the HTTP client SHALL make exactly K+1
 * attempts, apply exponential backoff between retries, and return success.
 */
#include "cdo_ut.h"
#include "vendor/theft.h"

/*--- Testable retry logic (mirrors http_download's retry loop) ---*/

typedef int (*retry_attempt_func)(void *ctx);

typedef struct {
    int  result;
    int  attempts;
    int  backoff_ms[8];
    int  backoff_count;
} RetryResult;

static RetryResult retry_with_backoff(retry_attempt_func attempt_fn, void *ctx, int max_retries) {
    RetryResult r = {0};
    r.result = -1;
    r.attempts = 0;
    r.backoff_count = 0;

    if (max_retries < 0) max_retries = 0;
    int total_attempts = max_retries + 1;

    for (int attempt = 0; attempt < total_attempts; attempt++) {
        if (attempt > 0) {
            unsigned int delay_ms = 1000u * (1u << (unsigned)(attempt - 1));
            if (r.backoff_count < 8) {
                r.backoff_ms[r.backoff_count] = (int)delay_ms;
                r.backoff_count++;
            }
        }

        r.attempts++;
        int rc = attempt_fn(ctx);
        if (rc == 0) { r.result = 0; return r; }
    }

    r.result = -1;
    return r;
}

typedef struct {
    int fail_count;
    int call_count;
} MockDownloadCtx;

static int mock_download_attempt(void *ctx) {
    MockDownloadCtx *m = (MockDownloadCtx *)ctx;
    m->call_count++;
    if (m->call_count <= m->fail_count) return -1;
    return 0;
}

typedef struct { int k; } RetryTestInput;

static enum theft_alloc_res
alloc_retry_k(struct theft *t, void *env, void **output) {
    (void)env;
    RetryTestInput *input = malloc(sizeof(RetryTestInput));
    if (!input) return THEFT_ALLOC_ERROR;
    input->k = (int)theft_random_choice(t, 5);
    *output = input;
    return THEFT_ALLOC_OK;
}

static void free_retry_k(void *instance, void *env) { (void)env; free(instance); }

static void print_retry_k(FILE *f, const void *instance, void *env) {
    (void)env;
    fprintf(f, "K=%d", ((const RetryTestInput *)instance)->k);
}

static struct theft_type_info retry_k_type_info = {
    .alloc  = alloc_retry_k,
    .free   = free_retry_k,
    .print  = print_retry_k,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static enum theft_trial_res
prop_retry_logic_correctness(struct theft *t, void *arg1) {
    (void)t;
    const RetryTestInput *input = (const RetryTestInput *)arg1;
    int k = input->k;
    int max_retries = 3;

    MockDownloadCtx mock = { .fail_count = k, .call_count = 0 };
    RetryResult result = retry_with_backoff(mock_download_attempt, &mock, max_retries);

    if (k <= max_retries) {
        if (result.result != 0) {
            fprintf(stderr, "  FAIL: K=%d, expected success\n", k);
            return THEFT_TRIAL_FAIL;
        }
        if (result.attempts != k + 1) {
            fprintf(stderr, "  FAIL: K=%d, expected %d attempts, got %d\n", k, k + 1, result.attempts);
            return THEFT_TRIAL_FAIL;
        }
        if (result.backoff_count != k) {
            fprintf(stderr, "  FAIL: K=%d, expected %d backoffs, got %d\n", k, k, result.backoff_count);
            return THEFT_TRIAL_FAIL;
        }
        for (int i = 0; i < result.backoff_count; i++) {
            int expected_delay = (int)(1000u * (1u << (unsigned)i));
            if (result.backoff_ms[i] != expected_delay) {
                fprintf(stderr, "  FAIL: backoff[%d]=%d, expected %d\n", i, result.backoff_ms[i], expected_delay);
                return THEFT_TRIAL_FAIL;
            }
        }
    } else {
        if (result.result == 0) {
            fprintf(stderr, "  FAIL: K=%d, expected failure\n", k);
            return THEFT_TRIAL_FAIL;
        }
        if (result.attempts != max_retries + 1) {
            fprintf(stderr, "  FAIL: K=%d, expected %d attempts, got %d\n", k, max_retries + 1, result.attempts);
            return THEFT_TRIAL_FAIL;
        }
    }

    return THEFT_TRIAL_PASS;
}

TEST(prop_retry_logic_correctness) {
    struct theft_run_config cfg = {
        .name = "retry_logic_correctness",
        .prop = { .prop1 = prop_retry_logic_correctness },
        .type_info = { &retry_k_type_info },
        .seed = 17171,
        .trials = 500,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}
