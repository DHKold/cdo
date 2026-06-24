/*
 * theft.h - Minimal property-based testing for C
 *
 * A lightweight, single-header property-based testing library compatible
 * with the theft API concepts. Designed for Windows and cross-platform use.
 *
 * Based on the theft library by Scott Vokes (silentbicycle/theft)
 * ISC License
 *
 * This is a minimal vendored implementation suitable for the CDo project's
 * testing needs. It provides:
 *   - Random input generation via a seeded PRNG
 *   - Property function trial execution
 *   - Configurable trial counts
 *   - Counterexample reporting
 *   - Basic shrinking support
 */
#ifndef THEFT_H
#define THEFT_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

/* Version - compatible with theft 0.4.x API concepts */
#define THEFT_VERSION_MAJOR 0
#define THEFT_VERSION_MINOR 4
#define THEFT_VERSION_PATCH 99

#define THEFT_MAX_ARITY 7

/* Forward declarations */
struct theft;

typedef uint64_t theft_seed;
typedef uint64_t theft_hash;

/*============================================================================
 * Result enums
 *============================================================================*/

enum theft_trial_res {
    THEFT_TRIAL_PASS  = 0,
    THEFT_TRIAL_FAIL  = 1,
    THEFT_TRIAL_SKIP  = 2,
    THEFT_TRIAL_DUP   = 3,
    THEFT_TRIAL_ERROR = 4,
};

enum theft_run_res {
    THEFT_RUN_PASS           =  0,
    THEFT_RUN_FAIL           =  1,
    THEFT_RUN_SKIP           =  2,
    THEFT_RUN_ERROR          =  3,
    THEFT_RUN_ERROR_MEMORY   = -1,
    THEFT_RUN_ERROR_BAD_ARGS = -2,
};

enum theft_alloc_res {
    THEFT_ALLOC_OK,
    THEFT_ALLOC_SKIP,
    THEFT_ALLOC_ERROR,
};

enum theft_shrink_res {
    THEFT_SHRINK_OK,
    THEFT_SHRINK_DEAD_END,
    THEFT_SHRINK_NO_MORE_TACTICS,
    THEFT_SHRINK_ERROR,
};

/*============================================================================
 * Type info callbacks
 *============================================================================*/

typedef enum theft_alloc_res
theft_alloc_cb(struct theft *t, void *env, void **output);

typedef void
theft_free_cb(void *instance, void *env);

typedef theft_hash
theft_hash_cb(const void *instance, void *env);

typedef void
theft_print_cb(FILE *f, const void *instance, void *env);

typedef enum theft_shrink_res
theft_shrink_cb(struct theft *t, const void *instance,
    uint32_t tactic, void *env, void **output);

struct theft_type_info {
    theft_alloc_cb  *alloc;
    theft_free_cb   *free;
    theft_hash_cb   *hash;
    theft_print_cb  *print;
    theft_shrink_cb *shrink;
    void            *env;
};

/*============================================================================
 * Property function types
 *============================================================================*/

typedef enum theft_trial_res theft_propfun1(struct theft *t, void *arg1);
typedef enum theft_trial_res theft_propfun2(struct theft *t, void *arg1, void *arg2);
typedef enum theft_trial_res theft_propfun3(struct theft *t, void *arg1, void *arg2, void *arg3);

/*============================================================================
 * Run configuration
 *============================================================================*/

struct theft_run_config {
    const char *name;
    union {
        theft_propfun1 *prop1;
        theft_propfun2 *prop2;
        theft_propfun3 *prop3;
    } prop;
    struct theft_type_info *type_info[THEFT_MAX_ARITY];
    theft_seed seed;
    size_t trials;
    uint8_t arity;              /* inferred if 0 */
    void *hook_env;
};

/*============================================================================
 * PRNG - SplitMix64
 *============================================================================*/

struct theft {
    uint64_t rng_state;
    FILE *out;
};

static inline uint64_t theft__splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static inline uint64_t theft_random_bits(struct theft *t, uint8_t bits) {
    if (bits == 0) return 0;
    if (bits > 64) bits = 64;
    uint64_t v = theft__splitmix64(&t->rng_state);
    if (bits < 64) {
        v &= ((uint64_t)1 << bits) - 1;
    }
    return v;
}

static inline uint64_t theft_random(struct theft *t) {
    return theft_random_bits(t, 64);
}

static inline uint64_t theft_random_choice(struct theft *t, uint64_t ceil) {
    if (ceil <= 1) return 0;
    return theft_random_bits(t, 64) % ceil;
}

static inline double theft_random_double(struct theft *t) {
    uint64_t v = theft_random_bits(t, 53);
    return (double)v / (double)((uint64_t)1 << 53);
}

/*============================================================================
 * Hashing (FNV-1a 64-bit)
 *============================================================================*/

struct theft_hasher {
    theft_hash accum;
};

static inline theft_hash theft_hash_onepass(const uint8_t *data, size_t bytes) {
    theft_hash h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < bytes; i++) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static inline void theft_hash_init(struct theft_hasher *h) {
    h->accum = 0xcbf29ce484222325ULL;
}

static inline void theft_hash_sink(struct theft_hasher *h, const uint8_t *data, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) {
        h->accum ^= data[i];
        h->accum *= 0x100000001b3ULL;
    }
}

static inline theft_hash theft_hash_done(struct theft_hasher *h) {
    theft_hash result = h->accum;
    h->accum = 0xcbf29ce484222325ULL;
    return result;
}

/*============================================================================
 * Run implementation
 *============================================================================*/

static inline enum theft_run_res
theft_run(const struct theft_run_config *cfg) {
    if (cfg == NULL) return THEFT_RUN_ERROR_BAD_ARGS;

    /* Determine arity */
    uint8_t arity = cfg->arity;
    if (arity == 0) {
        for (int i = 0; i < THEFT_MAX_ARITY; i++) {
            if (cfg->type_info[i] != NULL) {
                arity = (uint8_t)(i + 1);
            } else {
                break;
            }
        }
    }
    if (arity == 0 || arity > 3) return THEFT_RUN_ERROR_BAD_ARGS;

    size_t trials = cfg->trials > 0 ? cfg->trials : 100;
    theft_seed seed = cfg->seed;
    if (seed == 0) {
        seed = (theft_seed)time(NULL) ^ (theft_seed)((uintptr_t)cfg);
    }

    struct theft t = { .rng_state = seed, .out = stdout };

    size_t pass_count = 0;
    size_t fail_count = 0;
    size_t skip_count = 0;
    void *args[THEFT_MAX_ARITY] = {0};

    /* Track the seed that produced the first failure for reporting */
    theft_seed fail_seed = 0;

    for (size_t trial = 0; trial < trials; trial++) {
        /* Advance PRNG state for each trial */
        theft_seed trial_seed = seed + trial;
        t.rng_state = trial_seed;

        /* Allocate instances */
        bool alloc_ok = true;
        bool alloc_skip = false;
        for (uint8_t i = 0; i < arity; i++) {
            struct theft_type_info *ti = cfg->type_info[i];
            enum theft_alloc_res ares = ti->alloc(&t, ti->env, &args[i]);
            if (ares == THEFT_ALLOC_SKIP) { alloc_skip = true; break; }
            if (ares == THEFT_ALLOC_ERROR) { alloc_ok = false; break; }
        }

        if (!alloc_ok) {
            /* Free any allocated args */
            for (uint8_t i = 0; i < arity; i++) {
                if (args[i] && cfg->type_info[i]->free) {
                    cfg->type_info[i]->free(args[i], cfg->type_info[i]->env);
                    args[i] = NULL;
                }
            }
            return THEFT_RUN_ERROR_MEMORY;
        }
        if (alloc_skip) {
            for (uint8_t i = 0; i < arity; i++) {
                if (args[i] && cfg->type_info[i]->free) {
                    cfg->type_info[i]->free(args[i], cfg->type_info[i]->env);
                    args[i] = NULL;
                }
            }
            skip_count++;
            continue;
        }

        /* Run property */
        enum theft_trial_res tres = THEFT_TRIAL_ERROR;
        switch (arity) {
        case 1:
            tres = cfg->prop.prop1(&t, args[0]);
            break;
        case 2:
            tres = cfg->prop.prop2(&t, args[0], args[1]);
            break;
        case 3:
            tres = cfg->prop.prop3(&t, args[0], args[1], args[2]);
            break;
        default:
            break;
        }

        switch (tres) {
        case THEFT_TRIAL_PASS:
            pass_count++;
            break;
        case THEFT_TRIAL_FAIL:
            fail_count++;
            if (fail_count == 1) {
                fail_seed = trial_seed;
                /* Print counterexample */
                if (cfg->name) {
                    fprintf(stderr, "\n-- FAIL: %s (seed: 0x%016" PRIx64 ")\n",
                            cfg->name, trial_seed);
                }
                for (uint8_t i = 0; i < arity; i++) {
                    if (cfg->type_info[i]->print) {
                        fprintf(stderr, "  arg[%d]: ", i);
                        cfg->type_info[i]->print(stderr, args[i], cfg->type_info[i]->env);
                        fprintf(stderr, "\n");
                    }
                }
            }
            break;
        case THEFT_TRIAL_SKIP:
            skip_count++;
            break;
        case THEFT_TRIAL_DUP:
            break;
        case THEFT_TRIAL_ERROR:
            /* Free args and bail */
            for (uint8_t i = 0; i < arity; i++) {
                if (args[i] && cfg->type_info[i]->free) {
                    cfg->type_info[i]->free(args[i], cfg->type_info[i]->env);
                    args[i] = NULL;
                }
            }
            return THEFT_RUN_ERROR;
        }

        /* Free allocated instances */
        for (uint8_t i = 0; i < arity; i++) {
            if (args[i] && cfg->type_info[i]->free) {
                cfg->type_info[i]->free(args[i], cfg->type_info[i]->env);
                args[i] = NULL;
            }
        }

        /* Early termination on first failure (for faster feedback) */
        if (fail_count > 0 && trial >= 10) break;
    }

    (void)fail_seed;

    if (fail_count > 0) return THEFT_RUN_FAIL;
    if (pass_count > 0) return THEFT_RUN_PASS;
    return THEFT_RUN_SKIP;
}

/*============================================================================
 * Convenience macros for test registration
 *============================================================================*/

/* Test registration infrastructure */
typedef struct {
    const char *name;
    int (*func)(void);
} theft_test_entry;

#define THEFT_TEST(name) static int name(void)

#define THEFT_RUN_CHECK(cfg_ptr) do {                 \
    enum theft_run_res res = theft_run((cfg_ptr));    \
    if (res != THEFT_RUN_PASS) return (int)res;      \
} while (0)

#endif /* THEFT_H */
