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
)
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
    )
    target_include_directories(cdo_pbt PRIVATE
        ${CMAKE_SOURCE_DIR}/tests
    )
    set_target_properties(cdo_pbt PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    add_test(NAME cdo_pbt COMMAND cdo_pbt)

endif()

