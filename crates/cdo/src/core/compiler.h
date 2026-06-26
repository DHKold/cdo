#ifndef CDO_CORE_COMPILER_H
#define CDO_CORE_COMPILER_H

#include "core/workspace.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Compiler Family ---
typedef enum {
    COMPILER_GCC,
    COMPILER_CLANG,
    COMPILER_MSVC,
    COMPILER_UNKNOWN,
} CompilerFamily;

// --- Compiler Info ---
typedef struct {
    CompilerFamily  family;
    char            path[260];
    char            version[32];
    char            linker_path[260];
} CompilerInfo;

// --- Compile Job ---
typedef struct {
    const char*     source_path;
    const char*     object_path;
    const char**    include_paths;
    int             include_path_count;
    const char**    defines;
    int             define_count;
    const char*     c_standard;     // "c11", "c17", "c23", etc.
    const char*     cpp_standard;   // "c++17", "c++20", "c++23", etc.
    const char**    extra_flags;
    int             extra_flag_count;
    bool            optimize;
    bool            debug_info;
} CompileJob;

// --- Link Job ---
typedef struct {
    const char**    object_paths;
    int             object_count;
    const char*     output_path;
    const char**    lib_paths;
    int             lib_path_count;
    const char**    link_libs;
    int             link_lib_count;
    bool            shared;
} LinkJob;

// --- Build Unit (for incremental compilation dirty-set tracking) ---
typedef struct {
    char            source_path[260];
    uint64_t        source_mtime;
    uint64_t        object_mtime;
    uint64_t*       header_mtimes;
    int             header_dep_count;
    bool            object_exists;
    bool            needs_rebuild;
} BuildUnit;

/// Detect the system compiler by probing the PATH for known compilers.
/// On Windows: checks MSVC (cl.exe), then GCC (MinGW), then Clang.
/// On POSIX: checks GCC, then Clang.
/// Populates info with the detected compiler's family, path, version, and linker path.
/// Returns 0 on success, non-zero if no compiler is found.
int compiler_detect(CompilerInfo* info);

/// Compute which sources need recompilation. Returns array of dirty indices.
/// Caller is responsible for freeing *dirty_indices.
int compiler_compute_dirty(const Crate* crate, const char* build_dir,
                           int** dirty_indices, int* dirty_count);

/// Compute the dirty set from BuildUnit array. Returns the number of dirty units.
/// dirty_out must be pre-allocated with at least unit_count entries.
int compiler_compute_dirty_set(const BuildUnit* units, int unit_count,
                               int* dirty_out);

/// Execute a batch of compile jobs in parallel via thread pool.
/// Generates compiler command lines based on the compiler family and dispatches
/// compilation jobs to the thread pool for parallel execution.
/// Returns 0 if all compile jobs succeed, non-zero if any fail.
int compiler_compile_batch(const CompileJob* jobs, int job_count,
                           const CompilerInfo* info, int parallelism);

/// Link object files into final artifact (executable or library).
/// Returns 0 on success, non-zero on failure.
int compiler_link(const LinkJob* job, const CompilerInfo* info);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_COMPILER_H
