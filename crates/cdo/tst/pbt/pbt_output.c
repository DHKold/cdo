/*
 * pbt_output.c - Property: Quiet Mode Filters Non-Errors
 * Validates: Requirements 14.3
 *
 * For any log message at a level other than ERROR, when quiet mode is active
 * (log level = CDO_LOG_LEVEL_ERROR), the output renderer SHALL suppress the message.
 * Only ERROR-level messages SHALL pass through.
 */
#include "cdo_ut.h"
#include "vendor/theft.h"
#include "core/log.h"

/* Allocate a random non-ERROR log level (WARN, INFO, DEBUG, TRACE) */
static enum theft_alloc_res
alloc_non_error_level(struct theft *t, void *env, void **output) {
    (void)env;
    int *level = malloc(sizeof(int));
    if (!level) return THEFT_ALLOC_ERROR;
    /* Pick from CDO_LOG_LEVEL_WARN(1), CDO_LOG_LEVEL_INFO(2), CDO_LOG_LEVEL_DEBUG(3), CDO_LOG_LEVEL_TRACE(4) */
    *level = (int)(theft_random_choice(t, 4) + 1);
    *output = level;
    return THEFT_ALLOC_OK;
}

static void free_level(void *instance, void *env) {
    (void)env;
    free(instance);
}

static void print_level(FILE *f, const void *instance, void *env) {
    (void)env;
    int lvl = *(const int *)instance;
    const char *names[] = {"ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
    if (lvl >= 0 && lvl <= 4) {
        fprintf(f, "%s(%d)", names[lvl], lvl);
    } else {
        fprintf(f, "UNKNOWN(%d)", lvl);
    }
}

static struct theft_type_info non_error_level_type_info = {
    .alloc  = alloc_non_error_level,
    .free   = free_level,
    .print  = print_level,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

/* Property: In quiet mode, non-ERROR messages are suppressed (emit count stays 0) */
static enum theft_trial_res
prop_quiet_mode_suppresses_non_errors(struct theft *t, void *arg1) {
    (void)t;
    int level = *(int *)arg1;

    /* Initialize logging in quiet mode (only ERROR passes through) */
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);
    cdo_log_test_reset_emit_count();

    /* Attempt to log at the non-ERROR level */
    cdo_log((CdoLogLevel)level, "test message at level %d", level);

    /* In quiet mode, non-ERROR messages must be suppressed */
    if (cdo_log_test_get_emit_count() != 0) {
        return THEFT_TRIAL_FAIL;
    }

    return THEFT_TRIAL_PASS;
}

/* Also verify that ERROR messages DO pass through in quiet mode */
TEST(prop_quiet_mode_filters_non_errors) {
    /* Part 1: Property test - non-ERROR levels are suppressed */
    struct theft_run_config cfg = {
        .name = "quiet_mode_suppresses_non_errors",
        .prop = { .prop1 = prop_quiet_mode_suppresses_non_errors },
        .type_info = { &non_error_level_type_info },
        .seed = 99999,
        .trials = 200,
    };
    enum theft_run_res res = theft_run(&cfg);
    if (res != THEFT_RUN_PASS) return 1;

    /* Part 2: Verify ERROR messages still pass through */
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);
    cdo_log_test_reset_emit_count();
    cdo_log(CDO_LOG_LEVEL_ERROR, "this error should pass through");
    if (cdo_log_test_get_emit_count() != 1) return 2;

    return 0;
}
