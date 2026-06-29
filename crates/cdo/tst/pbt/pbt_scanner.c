/*
 * pbt_scanner.c - Property: Source Scanner Completeness
 * Validates: Requirements 7.1
 *
 * For any directory tree rooted at a crate's src/ directory, the source scanner
 * SHALL discover every file with extension .c, .cpp, .h, or .hpp — the returned
 * file list SHALL be a superset of all such files (minus those matching exclude
 * patterns). Non-source files SHALL NOT appear in the output.
 */
#include "cdo_ut.h"
#include "vendor/theft.h"
#include "model/scanner.h"
#include "pal/pal.h"

/* Structure representing a generated temp directory tree for testing */
typedef struct {
    char   tmp_dir[512];
    char** source_files;
    int    source_count;
    char** non_source_files;
    int    non_source_count;
} ScannerTestTree;

static const char* SOURCE_EXTS[] = { ".c", ".cpp", ".h", ".hpp" };
#define SOURCE_EXT_COUNT 4

static const char* NON_SOURCE_EXTS[] = { ".txt", ".md", ".o", ".obj", ".py", ".json" };
#define NON_SOURCE_EXT_COUNT 6

static void gen_filename_part(struct theft *t, char *buf, size_t bufsize) {
    size_t len = (size_t)(theft_random_choice(t, 7) + 1);
    if (len >= bufsize) len = bufsize - 1;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)('a' + theft_random_choice(t, 26));
    }
    buf[len] = '\0';
}

static enum theft_alloc_res
alloc_scanner_test_tree(struct theft *t, void *env, void **output) {
    (void)env;

    ScannerTestTree *tree = calloc(1, sizeof(ScannerTestTree));
    if (!tree) return THEFT_ALLOC_ERROR;

    uint64_t unique_id = theft_random_bits(t, 48);
#ifdef _WIN32
    snprintf(tree->tmp_dir, sizeof(tree->tmp_dir),
             "%s\\cdo_pbt_scanner_%llu",
             getenv("TEMP") ? getenv("TEMP") : "C:\\Temp",
             (unsigned long long)unique_id);
#else
    snprintf(tree->tmp_dir, sizeof(tree->tmp_dir),
             "/tmp/cdo_pbt_scanner_%llu", (unsigned long long)unique_id);
#endif

    char src_dir[600];
    pal_path_join(src_dir, sizeof(src_dir), tree->tmp_dir, "src");

    if (pal_mkdir_p(src_dir) != PAL_OK) {
        free(tree);
        return THEFT_ALLOC_ERROR;
    }

    int subdir_count = (int)theft_random_choice(t, 4);
    char subdirs[4][600];
    int actual_subdirs = 0;

    strncpy(subdirs[0], src_dir, sizeof(subdirs[0]) - 1);
    subdirs[0][sizeof(subdirs[0]) - 1] = '\0';
    actual_subdirs = 1;

    for (int i = 0; i < subdir_count; i++) {
        char dirname[16];
        gen_filename_part(t, dirname, sizeof(dirname));
        char subpath[600];
        pal_path_join(subpath, sizeof(subpath), src_dir, dirname);
        if (pal_mkdir_p(subpath) == PAL_OK) {
            strncpy(subdirs[actual_subdirs], subpath, sizeof(subdirs[actual_subdirs]) - 1);
            subdirs[actual_subdirs][sizeof(subdirs[actual_subdirs]) - 1] = '\0';
            actual_subdirs++;
        }
    }

    int src_file_count = (int)(theft_random_choice(t, 6) + 1);
    tree->source_files = calloc((size_t)src_file_count, sizeof(char*));
    if (!tree->source_files) {
        pal_rmdir_r(tree->tmp_dir);
        free(tree);
        return THEFT_ALLOC_ERROR;
    }
    tree->source_count = 0;

    for (int i = 0; i < src_file_count; i++) {
        int dir_idx = (int)theft_random_choice(t, (uint64_t)actual_subdirs);
        char name[16];
        gen_filename_part(t, name, sizeof(name));
        int ext_idx = (int)theft_random_choice(t, SOURCE_EXT_COUNT);

        char filepath[700];
        snprintf(filepath, sizeof(filepath), "%s/%s%s",
                 subdirs[dir_idx], name, SOURCE_EXTS[ext_idx]);

        const char *content = "/* source */\n";
        if (pal_file_write(filepath, content, strlen(content)) == PAL_OK) {
            tree->source_files[tree->source_count] = strdup(filepath);
            if (!tree->source_files[tree->source_count]) {
                for (int j = 0; j < tree->source_count; j++) free(tree->source_files[j]);
                free(tree->source_files);
                pal_rmdir_r(tree->tmp_dir);
                free(tree);
                return THEFT_ALLOC_ERROR;
            }
            tree->source_count++;
        }
    }

    int non_src_count = (int)(theft_random_choice(t, 4) + 1);
    tree->non_source_files = calloc((size_t)non_src_count, sizeof(char*));
    if (!tree->non_source_files) {
        for (int i = 0; i < tree->source_count; i++) free(tree->source_files[i]);
        free(tree->source_files);
        pal_rmdir_r(tree->tmp_dir);
        free(tree);
        return THEFT_ALLOC_ERROR;
    }
    tree->non_source_count = 0;

    for (int i = 0; i < non_src_count; i++) {
        int dir_idx = (int)theft_random_choice(t, (uint64_t)actual_subdirs);
        char name[16];
        gen_filename_part(t, name, sizeof(name));
        int ext_idx = (int)theft_random_choice(t, NON_SOURCE_EXT_COUNT);

        char filepath[700];
        snprintf(filepath, sizeof(filepath), "%s/%s%s",
                 subdirs[dir_idx], name, NON_SOURCE_EXTS[ext_idx]);

        const char *content = "non-source content\n";
        if (pal_file_write(filepath, content, strlen(content)) == PAL_OK) {
            tree->non_source_files[tree->non_source_count] = strdup(filepath);
            if (!tree->non_source_files[tree->non_source_count]) {
                for (int j = 0; j < tree->non_source_count; j++) free(tree->non_source_files[j]);
                free(tree->non_source_files);
                for (int j = 0; j < tree->source_count; j++) free(tree->source_files[j]);
                free(tree->source_files);
                pal_rmdir_r(tree->tmp_dir);
                free(tree);
                return THEFT_ALLOC_ERROR;
            }
            tree->non_source_count++;
        }
    }

    if (tree->source_count == 0) {
        for (int i = 0; i < tree->non_source_count; i++) free(tree->non_source_files[i]);
        free(tree->non_source_files);
        free(tree->source_files);
        pal_rmdir_r(tree->tmp_dir);
        free(tree);
        return THEFT_ALLOC_SKIP;
    }

    *output = tree;
    return THEFT_ALLOC_OK;
}

static void free_scanner_test_tree(void *instance, void *env) {
    (void)env;
    ScannerTestTree *tree = (ScannerTestTree *)instance;
    if (!tree) return;
    pal_rmdir_r(tree->tmp_dir);
    for (int i = 0; i < tree->source_count; i++) free(tree->source_files[i]);
    free(tree->source_files);
    for (int i = 0; i < tree->non_source_count; i++) free(tree->non_source_files[i]);
    free(tree->non_source_files);
    free(tree);
}

static void print_scanner_test_tree(FILE *f, const void *instance, void *env) {
    (void)env;
    const ScannerTestTree *tree = (const ScannerTestTree *)instance;
    fprintf(f, "ScannerTestTree(dir=\"%s\", sources=%d, non_sources=%d)",
            tree->tmp_dir, tree->source_count, tree->non_source_count);
}

static struct theft_type_info scanner_test_tree_type_info = {
    .alloc  = alloc_scanner_test_tree,
    .free   = free_scanner_test_tree,
    .print  = print_scanner_test_tree,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static bool filelist_contains_path(const FileList *fl, const char *path) {
    size_t plen = strlen(path);
    char *norm_expected = (char*)malloc(plen + 1);
    if (!norm_expected) return false;
    memcpy(norm_expected, path, plen + 1);
    pal_path_normalize(norm_expected);

    for (int i = 0; i < fl->count; i++) {
        if (strcmp(fl->paths[i], norm_expected) == 0) {
            free(norm_expected);
            return true;
        }
    }
    free(norm_expected);
    return false;
}

static bool path_has_source_ext(const char *path) {
    const char *ext = pal_path_ext(path);
    return (strcmp(ext, ".c") == 0 ||
            strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".h") == 0 ||
            strcmp(ext, ".hpp") == 0);
}

static enum theft_trial_res
prop_scanner_completeness(struct theft *t, void *arg1) {
    (void)t;
    ScannerTestTree *tree = (ScannerTestTree *)arg1;

    FileList result = {0};
    int rc = scanner_scan_sources(tree->tmp_dir, NULL, 0, &result);
    if (rc != 0) {
        fprintf(stderr, "  scanner_scan_sources failed with rc=%d for dir: %s\n",
                rc, tree->tmp_dir);
        return THEFT_TRIAL_FAIL;
    }

    for (int i = 0; i < tree->source_count; i++) {
        if (!filelist_contains_path(&result, tree->source_files[i])) {
            fprintf(stderr, "  FAIL: source file not found: %s\n", tree->source_files[i]);
            fprintf(stderr, "  Scanner found %d files:\n", result.count);
            for (int j = 0; j < result.count; j++) {
                fprintf(stderr, "    [%d] %s\n", j, result.paths[j]);
            }
            filelist_free(&result);
            return THEFT_TRIAL_FAIL;
        }
    }

    for (int i = 0; i < tree->non_source_count; i++) {
        if (filelist_contains_path(&result, tree->non_source_files[i])) {
            fprintf(stderr, "  FAIL: non-source file found in results: %s\n",
                    tree->non_source_files[i]);
            filelist_free(&result);
            return THEFT_TRIAL_FAIL;
        }
    }

    for (int i = 0; i < result.count; i++) {
        if (!path_has_source_ext(result.paths[i])) {
            fprintf(stderr, "  FAIL: result contains non-source file: %s\n",
                    result.paths[i]);
            filelist_free(&result);
            return THEFT_TRIAL_FAIL;
        }
    }

    filelist_free(&result);
    return THEFT_TRIAL_PASS;
}

TEST(prop_scanner_completeness) {
    struct theft_run_config cfg = {
        .name = "scanner_completeness",
        .prop = { .prop1 = prop_scanner_completeness },
        .type_info = { &scanner_test_tree_type_info },
        .seed = 70710,
        .trials = 100,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}
