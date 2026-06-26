cmake_minimum_required(VERSION 3.20)
project(cdo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CDO_ENABLE_TESTS "Build cdo-generated tests" ON)

include(cmake/cdo_deps.cmake OPTIONAL)

if(NOT COMMAND cdo_apply_dependencies)
    function(cdo_apply_dependencies target)
    endfunction()
endif()

if(NOT COMMAND cdo_copy_runtime_dependencies)
    function(cdo_copy_runtime_dependencies target)
    endfunction()
endif()

# --- Target: cdo (executable) ---
add_executable(cdo
    src/cdo/main.cpp
    src/cdo/commands/cmd_build.c
    src/cdo/commands/cmd_clean.c
    src/cdo/commands/cmd_deps.c
    src/cdo/commands/cmd_doctor.c
    src/cdo/commands/cmd_new.c
    src/cdo/commands/cmd_run.c
    src/cdo/commands/cmd_shader.c
    src/cdo/commands/cmd_test.c
    src/cdo/commands/cmd_tool.c
    src/cdo/core/cli.c
    src/cdo/core/output.c
    src/cdo/core/toml.c
    src/cdo/core/json.c
    src/cdo/core/scanner.c
    src/cdo/core/threadpool.c
    src/cdo/core/workspace.c
    src/cdo/core/compiler.c
    src/cdo/core/errors.c
    src/cdo/core/http.c
    src/cdo/core/archive.c
    src/cdo/core/template.c
    src/cdo/core/deps.c
    src/cdo/core/shader.c
    src/cdo/pal/pal_fs.c
    src/cdo/pal/pal_path.c
    src/cdo/pal/pal_process.c
    src/cdo/pal/pal_sysinfo.c
)
target_include_directories(cdo PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cdo
)
if(WIN32)
    target_link_libraries(cdo PRIVATE winhttp)
else()
    target_link_libraries(cdo PRIVATE m pthread)
endif()
cdo_apply_dependencies(cdo)
cdo_copy_runtime_dependencies(cdo)

if(CDO_ENABLE_TESTS)
    enable_testing()

    # --- Test: cdo_tests (legacy C++ placeholder) ---
    add_executable(cdo_tests
        tests/cdo/test_cdo.cpp
    )
    cdo_apply_dependencies(cdo_tests)
    add_test(NAME cdo_tests COMMAND cdo_tests)

    # --- Test: cdo_pbt (property-based tests, C17) ---
    add_executable(cdo_pbt
        tests/test_main.c
        src/cdo/pal/pal_path.c
        src/cdo/pal/pal_fs.c
        src/cdo/pal/pal_sysinfo.c
        src/cdo/pal/pal_process.c
        src/cdo/core/output.c
        src/cdo/core/toml.c
        src/cdo/core/cli.c
        src/cdo/core/scanner.c
        src/cdo/core/threadpool.c
        src/cdo/core/workspace.c
        src/cdo/core/archive.c
        src/cdo/core/compiler.c
        src/cdo/core/deps.c
        src/cdo/core/http.c
        src/cdo/core/template.c
        src/cdo/core/shader.c
        src/cdo/commands/cmd_clean.c
    )
    target_include_directories(cdo_pbt PRIVATE
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_SOURCE_DIR}/src/cdo
    )
    target_compile_definitions(cdo_pbt PRIVATE CDO_TESTING)
    set_target_properties(cdo_pbt PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    if(WIN32)
        target_link_libraries(cdo_pbt PRIVATE winhttp)
    else()
        target_link_libraries(cdo_pbt PRIVATE m pthread)
    endif()
    add_test(NAME cdo_pbt COMMAND cdo_pbt)

endif()

