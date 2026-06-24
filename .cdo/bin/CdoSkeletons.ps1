<#
Project skeleton definitions for cdo init.
#>

function Get-CdoSkeletonNames {
    return @('cli', 'cli-cpp', 'shared-lib', 'sdl3', 'sdl3-cpp', 'sdl3-gpu', 'sdl3-gpu-cpp')
}

function Resolve-CdoSkeletonName {
    param([string]$Name)

    if ([string]::IsNullOrWhiteSpace($Name)) { return 'cli' }

    $key = $Name.ToLowerInvariant()
    switch ($key) {
        'cli' { return 'cli' }
        'console' { return 'cli' }
        'command-line' { return 'cli' }
        'commandline' { return 'cli' }
        'sdl' { return 'sdl3' }
        'sdl3' { return 'sdl3' }
        'gpu' { return 'sdl3-gpu' }
        'sdl-gpu' { return 'sdl3-gpu' }
        'sdl3-gpu' { return 'sdl3-gpu' }
        'sdl3-shaders' { return 'sdl3-gpu' }
        'cli-cpp' { return 'cli-cpp' }
        'cpp' { return 'cli-cpp' }
        'c++' { return 'cli-cpp' }
        'sdl3-cpp' { return 'sdl3-cpp' }
        'sdl-cpp' { return 'sdl3-cpp' }
        'sdl3-gpu-cpp' { return 'sdl3-gpu-cpp' }
        'sdl-gpu-cpp' { return 'sdl3-gpu-cpp' }
        'gpu-cpp' { return 'sdl3-gpu-cpp' }
        'shared' { return 'shared-lib' }
        'shared-lib' { return 'shared-lib' }
        'shared-library' { return 'shared-lib' }
        'dll' { return 'shared-lib' }
        default {
            $valid = (Get-CdoSkeletonNames) -join ', '
            throw "Unknown skeleton '$Name'. Available skeletons: $valid"
        }
    }
}

function Get-CdoSkeletonDescription {
    param([string]$Name)

    switch (Resolve-CdoSkeletonName -Name $Name) {
        'cli' { return 'Simple command-line app with a core library and tests.' }
        'sdl3' { return 'SDL3 windowed app starter. Run cdo add sdl3 before building.' }
        'sdl3-gpu' { return 'SDL3 GPU/shader starter with HLSL shader sources and shader manifest.' }
        'cli-cpp' { return 'Simple C++ command-line app with a core library and tests.' }
        'sdl3-cpp' { return 'SDL3 windowed C++ app starter. Run cdo add sdl3 before building.' }
        'sdl3-gpu-cpp' { return 'SDL3 GPU/shader C++ starter with HLSL shader sources and shader manifest.' }
        'shared-lib' { return 'Shared library/DLL starter with export macros and tests.' }
    }
}

function Get-CdoHeaderGuard {
    param([string]$ProjectId)
    return ($ProjectId.ToUpperInvariant() + '_APP_H')
}

function Get-CdoExportMacroName {
    param([string]$ProjectId)
    return ($ProjectId.ToUpperInvariant() + '_API')
}

function New-CdoSkeletonManifest {
    param(
        [string]$ProjectName,
        [string]$ProjectId,
        [string]$Skeleton
    )

    $skeletonName = Resolve-CdoSkeletonName -Name $Skeleton
    $manifest = [ordered]@{
        schema = 'https://cdo.dev/schemas/project.v1.json'
        schemaVersion = 1
        name = $ProjectName
        id = $ProjectId
        version = '0.1.0'
        skeleton = $skeletonName
        cStandard = 17
        apps = @(
            [ordered]@{
                name = 'app'
                target = 'app'
                type = 'executable'
                workingDirectory = '.'
                args = @()
            }
        )
        tests = [ordered]@{
            enabled = $true
            target = 'app_tests'
        }
        build = [ordered]@{
            directory = 'build'
            defaultConfig = 'Debug'
            generator = ''
        }
        requiredDependencies = @()
        requiredTools = @()
        dependencies = [ordered]@{}
    }

    if ($skeletonName -eq 'sdl3' -or $skeletonName -eq 'sdl3-gpu') {
        $manifest.requiredDependencies = @('sdl3')
        if ($skeletonName -eq 'sdl3-gpu') {
            $manifest.requiredTools = @('dxc')
            $manifest.shaderToolchain = [ordered]@{
                sourceDir = 'assets/shaders/src'
                outputDir = 'assets/shaders/compiled'
                language = 'hlsl'
                entrypoint = 'main'
                targets = @('dxil', 'spirv')
            }
            $manifest.shaders = @(
                [ordered]@{
                    name = 'triangle'
                    stage = 'vertex'
                    source = 'assets/shaders/src/triangle.vert.hlsl'
                    entrypoint = 'main'
                },
                [ordered]@{
                    name = 'triangle'
                    stage = 'fragment'
                    source = 'assets/shaders/src/triangle.frag.hlsl'
                    entrypoint = 'main'
                }
            )
        }
    } elseif ($skeletonName -eq 'cli-cpp') {
        $manifest.Remove('apps')
        $manifest.Remove('tests')
        $manifest['cppStandard'] = 17
        $manifest['targets'] = @(
            [ordered]@{ name = 'app'; type = 'executable'; target = 'app' },
            [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests' }
        )
    } elseif ($skeletonName -eq 'sdl3-cpp') {
        $manifest.Remove('apps')
        $manifest.Remove('tests')
        $manifest['cppStandard'] = 17
        $manifest['targets'] = @(
            [ordered]@{ name = 'app'; type = 'executable'; target = 'app' },
            [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests' }
        )
        $manifest.requiredDependencies = @('sdl3')
    } elseif ($skeletonName -eq 'sdl3-gpu-cpp') {
        $manifest.Remove('apps')
        $manifest.Remove('tests')
        $manifest['cppStandard'] = 17
        $manifest['targets'] = @(
            [ordered]@{ name = 'app'; type = 'executable'; target = 'app' },
            [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests' }
        )
        $manifest.requiredDependencies = @('sdl3')
        $manifest.requiredTools = @('dxc')
        $manifest.shaderToolchain = [ordered]@{
            sourceDir = 'assets/shaders/src'
            outputDir = 'assets/shaders/compiled'
            language = 'hlsl'
            entrypoint = 'main'
            targets = @('dxil', 'spirv')
        }
        $manifest.shaders = @(
            [ordered]@{
                name = 'triangle'
                stage = 'vertex'
                source = 'assets/shaders/src/triangle.vert.hlsl'
                entrypoint = 'main'
            },
            [ordered]@{
                name = 'triangle'
                stage = 'fragment'
                source = 'assets/shaders/src/triangle.frag.hlsl'
                entrypoint = 'main'
            }
        )
    } elseif ($skeletonName -eq 'shared-lib') {
        $manifest.apps = @(
            [ordered]@{
                name = 'lib'
                target = $ProjectId
                type = 'shared-library'
            }
        )
        $manifest.tests.target = "${ProjectId}_tests"
    }

    return $manifest
}

function Get-CdoCliMainCContent {
    param([string]$ProjectId)
    return @"
#include <stdio.h>

#include "$ProjectId/app.h"

int main(int argc, char **argv) {
    printf("%s\n", ${ProjectId}_greeting());

    if (argc > 1) {
        printf("Received %d argument(s):\n", argc - 1);
        for (int i = 1; i < argc; ++i) {
            printf("  %d: %s\n", i, argv[i]);
        }
    }

    return 0;
}
"@
}

function Get-CdoCliAppCContent {
    param([string]$ProjectId, [string]$ProjectName)
    return @"
#include "$ProjectId/app.h"

const char *${ProjectId}_greeting(void) {
    return "Hello from $ProjectName. Build something sharp.";
}

int ${ProjectId}_add(int left, int right) {
    return left + right;
}
"@
}

function Get-CdoCliHeaderContent {
    param([string]$ProjectId)
    $guard = Get-CdoHeaderGuard -ProjectId $ProjectId
    return @"
#ifndef $guard
#define $guard

const char *${ProjectId}_greeting(void);
int ${ProjectId}_add(int left, int right);

#endif
"@
}

function Get-CdoCliTestContent {
    param([string]$ProjectId)
    return @"
#include <stdio.h>
#include <string.h>

#include "$ProjectId/app.h"

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "assertion failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_INT(expected, actual) do { \
    int expected_value = (expected); \
    int actual_value = (actual); \
    if (expected_value != actual_value) { \
        fprintf(stderr, "assertion failed: expected %d, got %d at %s:%d\n", expected_value, actual_value, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

int main(void) {
    ASSERT_TRUE(${ProjectId}_greeting() != NULL);
    ASSERT_TRUE(strlen(${ProjectId}_greeting()) > 0);
    ASSERT_EQ_INT(4, ${ProjectId}_add(2, 2));

    puts("app_tests: all tests passed");
    return 0;
}
"@
}

function Get-CdoExecutableCMakeContent {
    param([string]$ProjectId)
    return @"
cmake_minimum_required(VERSION 3.20)
project($ProjectId VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

add_library(${ProjectId}_core
    src/app.c
)
target_include_directories(${ProjectId}_core
    PUBLIC
        include
)
cdo_apply_dependencies(${ProjectId}_core)

add_executable(app
    src/main.c
)
target_link_libraries(app
    PRIVATE
        ${ProjectId}_core
)
cdo_apply_dependencies(app)
cdo_copy_runtime_dependencies(app)

if(CDO_ENABLE_TESTS)
    enable_testing()

    add_executable(app_tests
        tests/test_app.c
    )
    target_link_libraries(app_tests
        PRIVATE
            ${ProjectId}_core
    )
    cdo_apply_dependencies(app_tests)
    cdo_copy_runtime_dependencies(app_tests)

    add_test(NAME app_tests COMMAND app_tests)
endif()
"@
}

function Get-CdoSdlMainCContent {
    param([string]$ProjectId)
    return @"
#include "$ProjectId/app.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return ${ProjectId}_run();
}
"@
}

function Get-CdoSdlAppCContent {
    param([string]$ProjectId, [string]$ProjectName)
    return @"
#include <stdio.h>

#include <SDL3/SDL.h>

#include "$ProjectId/app.h"

const char *${ProjectId}_greeting(void) {
    return "$ProjectName";
}

int ${ProjectId}_add(int left, int right) {
    return left + right;
}

int ${ProjectId}_run(void) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(${ProjectId}_greeting(), 960, 540, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Uint64 start = SDL_GetTicks();
    int running = 1;
    while (running && SDL_GetTicks() - start < 2000) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = 0;
            }
        }

        SDL_SetRenderDrawColor(renderer, 24, 28, 36, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 64, 196, 160, 255);
        SDL_FRect rect = { 380.0f, 190.0f, 200.0f, 160.0f };
        SDL_RenderFillRect(renderer, &rect);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
"@
}

function Get-CdoSdlHeaderContent {
    param([string]$ProjectId)
    $guard = Get-CdoHeaderGuard -ProjectId $ProjectId
    return @"
#ifndef $guard
#define $guard

const char *${ProjectId}_greeting(void);
int ${ProjectId}_add(int left, int right);
int ${ProjectId}_run(void);

#endif
"@
}

function Get-CdoSdlCppMainContent {
    param([string]$ProjectId)
    return @"
#include "$ProjectId/app.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return ${ProjectId}_run();
}
"@
}

function Get-CdoSdlCppAppContent {
    param([string]$ProjectId, [string]$ProjectName)
    return @"
#include <cstdio>

#include <SDL3/SDL.h>

#include "$ProjectId/app.h"

const char *${ProjectId}_greeting() {
    return "$ProjectName";
}

int ${ProjectId}_add(int left, int right) {
    return left + right;
}

int ${ProjectId}_run() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(${ProjectId}_greeting(), 960, 540, 0);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Uint64 start = SDL_GetTicks();
    int running = 1;
    while (running && SDL_GetTicks() - start < 2000) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = 0;
            }
        }

        SDL_SetRenderDrawColor(renderer, 24, 28, 36, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 64, 196, 160, 255);
        SDL_FRect rect = { 380.0f, 190.0f, 200.0f, 160.0f };
        SDL_RenderFillRect(renderer, &rect);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
"@
}

function Get-CdoSdlCppHeaderContent {
    param([string]$ProjectId)
    $guard = Get-CdoHeaderGuard -ProjectId $ProjectId
    return @"
#ifndef $guard
#define $guard

const char *${ProjectId}_greeting();
int ${ProjectId}_add(int left, int right);
int ${ProjectId}_run();

#endif
"@
}

function Get-CdoSdlCppTestContent {
    param([string]$ProjectId)
    return @"
#include <iostream>
#include <cstring>

#include "$ProjectId/app.h"

int main() {
    if (${ProjectId}_greeting() == nullptr || std::strlen(${ProjectId}_greeting()) == 0) {
        std::cerr << "expected a non-empty greeting" << std::endl;
        return 1;
    }

    if (${ProjectId}_add(2, 2) != 4) {
        std::cerr << "expected add(2, 2) to equal 4" << std::endl;
        return 1;
    }

    std::cout << "app_tests: all tests passed" << std::endl;
    return 0;
}
"@
}

function Get-CdoSdlGpuMainCContent {
    param([string]$ProjectId)
    return @"
#include "$ProjectId/gpu_app.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return ${ProjectId}_gpu_run();
}
"@
}

function Get-CdoSdlGpuHeaderContent {
    param([string]$ProjectId)
    $guard = ($ProjectId.ToUpperInvariant() + '_GPU_APP_H')
    return @"
#ifndef $guard
#define $guard

int ${ProjectId}_gpu_run(void);

#endif
"@
}

function Get-CdoShaderLoaderHeaderContent {
    param([string]$ProjectId)
    $guard = ($ProjectId.ToUpperInvariant() + '_SHADER_LOADER_H')
    return @"
#ifndef $guard
#define $guard

#include <stddef.h>

typedef struct ${ProjectId}_FileBytes {
    unsigned char *data;
    size_t size;
} ${ProjectId}_FileBytes;

int ${ProjectId}_read_file(const char *path, ${ProjectId}_FileBytes *out);
void ${ProjectId}_free_file(${ProjectId}_FileBytes *bytes);

#endif
"@
}

function Get-CdoShaderLoaderSourceContent {
    param([string]$ProjectId)
    return @"
#include "$ProjectId/shader_loader.h"

#include <stdio.h>
#include <stdlib.h>

int ${ProjectId}_read_file(const char *path, ${ProjectId}_FileBytes *out) {
    FILE *file = fopen(path, "rb");
    long size = 0;

    if (!out) {
        return 0;
    }
    out->data = NULL;
    out->size = 0;

    if (!file) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    out->data = (unsigned char *)malloc((size_t)size);
    if (!out->data) {
        fclose(file);
        return 0;
    }

    out->size = fread(out->data, 1, (size_t)size, file);
    fclose(file);
    return out->size == (size_t)size;
}

void ${ProjectId}_free_file(${ProjectId}_FileBytes *bytes) {
    if (!bytes) {
        return;
    }
    free(bytes->data);
    bytes->data = NULL;
    bytes->size = 0;
}
"@
}

function Get-CdoSdlGpuSourceContent {
    param([string]$ProjectId, [string]$ProjectName)
    return @"
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include "$ProjectId/gpu_app.h"
#include "$ProjectId/shader_loader.h"

typedef struct DemoVertex {
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
} DemoVertex;

static const DemoVertex DEMO_VERTICES[] = {
    { -0.72f, -0.48f, 0.0f, 0.95f, 0.20f, 0.20f },
    { -0.12f, -0.48f, 0.0f, 0.95f, 0.80f, 0.20f },
    { -0.42f,  0.38f, 0.0f, 0.20f, 0.75f, 1.00f },

    {  0.24f, -0.18f, 0.0f, 0.25f, 0.95f, 0.45f },
    {  0.50f,  0.18f, 0.0f, 0.20f, 0.70f, 1.00f },
    {  0.76f, -0.18f, 0.0f, 0.95f, 0.25f, 0.80f },
    {  0.24f, -0.18f, 0.0f, 0.25f, 0.95f, 0.45f },
    {  0.76f, -0.18f, 0.0f, 0.95f, 0.25f, 0.80f },
    {  0.50f, -0.54f, 0.0f, 0.95f, 0.65f, 0.20f },

    { -0.18f,  0.56f, 0.0f, 0.75f, 0.40f, 1.00f },
    {  0.78f,  0.56f, 0.0f, 0.25f, 0.95f, 0.85f },
    {  0.78f,  0.76f, 0.0f, 0.95f, 0.95f, 0.35f },
    { -0.18f,  0.56f, 0.0f, 0.75f, 0.40f, 1.00f },
    {  0.78f,  0.76f, 0.0f, 0.95f, 0.95f, 0.35f },
    { -0.18f,  0.76f, 0.0f, 0.25f, 0.55f, 1.00f }
};

static int read_shader_artifact(const char *relative_path, ${ProjectId}_FileBytes *out) {
    const char *base_path = NULL;
    char full_path[1024];

    if (${ProjectId}_read_file(relative_path, out)) {
        return 1;
    }

    base_path = SDL_GetBasePath();
    if (!base_path) {
        return 0;
    }

    SDL_snprintf(full_path, sizeof(full_path), "%s%s", base_path, relative_path);
    return ${ProjectId}_read_file(full_path, out);
}

static SDL_GPUShader *load_demo_shader(
    SDL_GPUDevice *device,
    SDL_GPUShaderFormat format,
    SDL_GPUShaderStage stage,
    const char *stage_name,
    const char *target_dir,
    const char *extension) {
    char path[256];
    ${ProjectId}_FileBytes bytes = { 0 };
    SDL_GPUShaderCreateInfo shader_info;
    SDL_GPUShader *shader = NULL;

    SDL_snprintf(path, sizeof(path), "assets/shaders/compiled/%s/triangle.%s.%s", target_dir, stage_name, extension);
    if (!read_shader_artifact(path, &bytes)) {
        fprintf(stderr, "Missing shader artifact: %s\n", path);
        fprintf(stderr, "Run 'cdo shader compile' or 'cdo build app' before running.\n");
        return NULL;
    }

    SDL_zero(shader_info);
    shader_info.code_size = bytes.size;
    shader_info.code = bytes.data;
    shader_info.entrypoint = "main";
    shader_info.format = format;
    shader_info.stage = stage;

    shader = SDL_CreateGPUShader(device, &shader_info);
    ${ProjectId}_free_file(&bytes);

    if (!shader) {
        fprintf(stderr, "SDL_CreateGPUShader failed for %s: %s\n", path, SDL_GetError());
    }
    return shader;
}

static int upload_demo_vertices(SDL_GPUDevice *device, SDL_GPUBuffer *vertex_buffer) {
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_GPUTransferBuffer *transfer_buffer = NULL;
    SDL_GPUCommandBuffer *command_buffer = NULL;
    SDL_GPUCopyPass *copy_pass = NULL;
    void *mapped = NULL;

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)sizeof(DEMO_VERTICES);

    transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!transfer_buffer) {
        fprintf(stderr, "SDL_CreateGPUTransferBuffer failed: %s\n", SDL_GetError());
        return 0;
    }

    mapped = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
    if (!mapped) {
        fprintf(stderr, "SDL_MapGPUTransferBuffer failed: %s\n", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
        return 0;
    }
    memcpy(mapped, DEMO_VERTICES, sizeof(DEMO_VERTICES));
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

    command_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (!command_buffer) {
        fprintf(stderr, "SDL_AcquireGPUCommandBuffer failed: %s\n", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
        return 0;
    }

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (copy_pass) {
        SDL_GPUTransferBufferLocation source = { transfer_buffer, 0 };
        SDL_GPUBufferRegion destination = { vertex_buffer, 0, (Uint32)sizeof(DEMO_VERTICES) };
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
        SDL_EndGPUCopyPass(copy_pass);
    }

    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        fprintf(stderr, "SDL_SubmitGPUCommandBuffer failed during upload: %s\n", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
        return 0;
    }

    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
    return copy_pass != NULL;
}

static int render_demo_frame(
    SDL_GPUDevice *device,
    SDL_Window *window,
    SDL_GPUGraphicsPipeline *pipeline,
    SDL_GPUBuffer *vertex_buffer) {
    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUTexture *swapchain_texture = NULL;
    Uint32 width = 0;
    Uint32 height = 0;

    if (!command_buffer) {
        fprintf(stderr, "SDL_AcquireGPUCommandBuffer failed: %s\n", SDL_GetError());
        return 0;
    }

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, &width, &height)) {
        fprintf(stderr, "SDL_WaitAndAcquireGPUSwapchainTexture failed: %s\n", SDL_GetError());
        SDL_CancelGPUCommandBuffer(command_buffer);
        return 0;
    }

    if (swapchain_texture) {
        SDL_GPUColorTargetInfo color_target;
        SDL_GPURenderPass *render_pass = NULL;

        SDL_zero(color_target);
        color_target.texture = swapchain_texture;
        color_target.clear_color = (SDL_FColor){ 0.05f, 0.06f, 0.08f, 1.0f };
        color_target.load_op = SDL_GPU_LOADOP_CLEAR;
        color_target.store_op = SDL_GPU_STOREOP_STORE;

        render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, NULL);
        if (render_pass) {
            SDL_GPUViewport viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
            SDL_GPUBufferBinding vertex_binding = { vertex_buffer, 0 };

            SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
            SDL_SetGPUViewport(render_pass, &viewport);
            SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
            SDL_DrawGPUPrimitives(render_pass, (Uint32)(sizeof(DEMO_VERTICES) / sizeof(DEMO_VERTICES[0])), 1, 0, 0);
            SDL_EndGPURenderPass(render_pass);
        }
    }

    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        fprintf(stderr, "SDL_SubmitGPUCommandBuffer failed: %s\n", SDL_GetError());
        return 0;
    }

    return 1;
}

int ${ProjectId}_gpu_run(void) {
    SDL_Window *window = NULL;
    SDL_GPUDevice *device = NULL;
    SDL_GPUShader *vertex_shader = NULL;
    SDL_GPUShader *fragment_shader = NULL;
    SDL_GPUGraphicsPipeline *pipeline = NULL;
    SDL_GPUBuffer *vertex_buffer = NULL;
    SDL_GPUShaderFormat shader_format = SDL_GPU_SHADERFORMAT_INVALID;
    const char *shader_target_dir = NULL;
    const char *shader_extension = NULL;
    Uint64 start = 0;
    int running = 1;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("$ProjectName", 960, 540, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_DXIL |
        SDL_GPU_SHADERFORMAT_SPIRV |
        SDL_GPU_SHADERFORMAT_MSL,
        true,
        NULL);
    if (!device) {
        fprintf(stderr, "SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        fprintf(stderr, "SDL_ClaimWindowForGPUDevice failed: %s\n", SDL_GetError());
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) {
        shader_format = SDL_GPU_SHADERFORMAT_DXIL;
        shader_target_dir = "dxil";
        shader_extension = "dxil";
    } else if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV) {
        shader_format = SDL_GPU_SHADERFORMAT_SPIRV;
        shader_target_dir = "spirv";
        shader_extension = "spv";
    } else {
        fprintf(stderr, "No supported shader format for this sample.\n");
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    vertex_shader = load_demo_shader(device, shader_format, SDL_GPU_SHADERSTAGE_VERTEX, "vertex", shader_target_dir, shader_extension);
    fragment_shader = load_demo_shader(device, shader_format, SDL_GPU_SHADERSTAGE_FRAGMENT, "fragment", shader_target_dir, shader_extension);
    if (!vertex_shader || !fragment_shader) {
        if (vertex_shader) { SDL_ReleaseGPUShader(device, vertex_shader); }
        if (fragment_shader) { SDL_ReleaseGPUShader(device, fragment_shader); }
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    {
        SDL_GPUBufferCreateInfo buffer_info;
        SDL_zero(buffer_info);
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        buffer_info.size = (Uint32)sizeof(DEMO_VERTICES);
        vertex_buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    }
    if (!vertex_buffer || !upload_demo_vertices(device, vertex_buffer)) {
        fprintf(stderr, "Could not prepare demo vertex buffer: %s\n", SDL_GetError());
        if (vertex_buffer) { SDL_ReleaseGPUBuffer(device, vertex_buffer); }
        SDL_ReleaseGPUShader(device, fragment_shader);
        SDL_ReleaseGPUShader(device, vertex_shader);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    {
        SDL_GPUVertexBufferDescription vertex_buffer_description;
        SDL_GPUVertexAttribute vertex_attributes[2];
        SDL_GPUColorTargetDescription color_target_description;
        SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

        SDL_zero(vertex_buffer_description);
        vertex_buffer_description.slot = 0;
        vertex_buffer_description.pitch = sizeof(DemoVertex);
        vertex_buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_zeroa(vertex_attributes);
        vertex_attributes[0].location = 0;
        vertex_attributes[0].buffer_slot = 0;
        vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        vertex_attributes[0].offset = offsetof(DemoVertex, x);
        vertex_attributes[1].location = 1;
        vertex_attributes[1].buffer_slot = 0;
        vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        vertex_attributes[1].offset = offsetof(DemoVertex, r);

        SDL_zero(color_target_description);
        color_target_description.format = SDL_GetGPUSwapchainTextureFormat(device, window);

        SDL_zero(pipeline_info);
        pipeline_info.vertex_shader = vertex_shader;
        pipeline_info.fragment_shader = fragment_shader;
        pipeline_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_description;
        pipeline_info.vertex_input_state.num_vertex_buffers = 1;
        pipeline_info.vertex_input_state.vertex_attributes = vertex_attributes;
        pipeline_info.vertex_input_state.num_vertex_attributes = 2;
        pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipeline_info.target_info.color_target_descriptions = &color_target_description;
        pipeline_info.target_info.num_color_targets = 1;

        pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);
    }

    SDL_ReleaseGPUShader(device, fragment_shader);
    SDL_ReleaseGPUShader(device, vertex_shader);
    if (!pipeline) {
        fprintf(stderr, "SDL_CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        SDL_ReleaseGPUBuffer(device, vertex_buffer);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    puts("SDL3 GPU sample running. The scene is drawn by a GPU pipeline using compiled shaders.");

    start = SDL_GetTicks();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = 0;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = 0;
            }
        }

        if (!render_demo_frame(device, window, pipeline, vertex_buffer)) {
            running = 0;
        }

        if (SDL_GetTicks() - start > 8000) {
            running = 0;
        }
    }

    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseGPUBuffer(device, vertex_buffer);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
"@
}

function Get-CdoSdlGpuTestContent {
    param([string]$ProjectId)
    return @"
#include <stdio.h>
#include <stdlib.h>

#include "$ProjectId/shader_loader.h"

int main(void) {
    ${ProjectId}_FileBytes bytes = { 0 };

    if (${ProjectId}_read_file("assets/shaders/src/triangle.vert.hlsl", &bytes)) {
        ${ProjectId}_free_file(&bytes);
    }

    puts("sdl3_gpu_tests: skeleton sanity checks passed");
    return 0;
}
"@
}

function Get-CdoSdlGpuCppMainContent {
    param([string]$ProjectId)
    return @"
#include "$ProjectId/gpu_app.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return ${ProjectId}_gpu_run();
}
"@
}

function Get-CdoSdlGpuCppHeaderContent {
    param([string]$ProjectId)
    $guard = ($ProjectId.ToUpperInvariant() + '_GPU_APP_H')
    return @"
#ifndef $guard
#define $guard

int ${ProjectId}_gpu_run();

#endif
"@
}

function Get-CdoShaderLoaderCppHeaderContent {
    param([string]$ProjectId)
    $guard = ($ProjectId.ToUpperInvariant() + '_SHADER_LOADER_H')
    return @"
#ifndef $guard
#define $guard

#include <cstddef>

struct ${ProjectId}_FileBytes {
    unsigned char *data;
    std::size_t size;
};

int ${ProjectId}_read_file(const char *path, ${ProjectId}_FileBytes *out);
void ${ProjectId}_free_file(${ProjectId}_FileBytes *bytes);

#endif
"@
}

function Get-CdoShaderLoaderCppSourceContent {
    param([string]$ProjectId)
    return @"
#include "$ProjectId/shader_loader.h"

#include <cstdio>
#include <cstdlib>

int ${ProjectId}_read_file(const char *path, ${ProjectId}_FileBytes *out) {
    FILE *file = std::fopen(path, "rb");
    long size = 0;

    if (!out) {
        return 0;
    }
    out->data = nullptr;
    out->size = 0;

    if (!file) {
        return 0;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return 0;
    }
    size = std::ftell(file);
    if (size < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return 0;
    }

    out->data = static_cast<unsigned char *>(std::malloc(static_cast<std::size_t>(size)));
    if (!out->data) {
        std::fclose(file);
        return 0;
    }

    out->size = std::fread(out->data, 1, static_cast<std::size_t>(size), file);
    std::fclose(file);
    return out->size == static_cast<std::size_t>(size);
}

void ${ProjectId}_free_file(${ProjectId}_FileBytes *bytes) {
    if (!bytes) {
        return;
    }
    std::free(bytes->data);
    bytes->data = nullptr;
    bytes->size = 0;
}
"@
}

function Get-CdoSdlGpuCppTestContent {
    param([string]$ProjectId)
    return @"
#include <iostream>

#include "$ProjectId/shader_loader.h"

int main() {
    ${ProjectId}_FileBytes bytes{};

    if (${ProjectId}_read_file("assets/shaders/src/triangle.vert.hlsl", &bytes)) {
        ${ProjectId}_free_file(&bytes);
    }

    std::cout << "sdl3_gpu_tests: skeleton sanity checks passed" << std::endl;
    return 0;
}
"@
}

function Get-CdoSdlGpuCMakeContent {
    param([string]$ProjectId)
    return @"
cmake_minimum_required(VERSION 3.20)
project($ProjectId VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

add_library(${ProjectId}_core
    src/gpu_app.c
    src/shader_loader.c
)
target_include_directories(${ProjectId}_core
    PUBLIC
        include
)
cdo_apply_dependencies(${ProjectId}_core)

add_executable(app
    src/main.c
)
target_link_libraries(app
    PRIVATE
        ${ProjectId}_core
)
cdo_apply_dependencies(app)
cdo_copy_runtime_dependencies(app)

add_custom_command(TARGET app POST_BUILD
    COMMAND "`${CMAKE_COMMAND}" -E copy_directory
            "`${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:app>/assets"
    VERBATIM)

if(CDO_ENABLE_TESTS)
    enable_testing()

    add_executable(app_tests
        tests/test_gpu_app.c
    )
    target_link_libraries(app_tests
        PRIVATE
            ${ProjectId}_core
    )
    cdo_apply_dependencies(app_tests)
    cdo_copy_runtime_dependencies(app_tests)

    add_test(NAME app_tests COMMAND app_tests)
endif()
"@
}

function Get-CdoDefaultVertexShaderContent {
    return @'
struct VSInput {
    float3 position : TEXCOORD0;
    float3 color : TEXCOORD1;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}
'@
}

function Get-CdoDefaultFragmentShaderContent {
    return @'
struct PSInput {
    float4 position : SV_Position;
    float3 color : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    return float4(input.color, 1.0);
}
'@
}

function Get-CdoSharedHeaderContent {
    param([string]$ProjectId)
    $guard = Get-CdoHeaderGuard -ProjectId $ProjectId
    $api = Get-CdoExportMacroName -ProjectId $ProjectId
    $buildMacro = $ProjectId.ToUpperInvariant() + '_BUILD'
    return @"
#ifndef $guard
#define $guard

#if defined(_WIN32)
#  if defined($buildMacro)
#    define $api __declspec(dllexport)
#  else
#    define $api __declspec(dllimport)
#  endif
#else
#  define $api
#endif

$api const char *${ProjectId}_version(void);
$api int ${ProjectId}_add(int left, int right);

#endif
"@
}

function Get-CdoSharedSourceContent {
    param([string]$ProjectId)
    return @"
#include "$ProjectId/$ProjectId.h"

const char *${ProjectId}_version(void) {
    return "0.1.0";
}

int ${ProjectId}_add(int left, int right) {
    return left + right;
}
"@
}

function Get-CdoSharedTestContent {
    param([string]$ProjectId)
    return @"
#include <stdio.h>
#include <string.h>

#include "$ProjectId/$ProjectId.h"

int main(void) {
    if (${ProjectId}_add(2, 2) != 4) {
        fprintf(stderr, "expected add(2, 2) to equal 4\n");
        return 1;
    }

    if (${ProjectId}_version() == NULL || strlen(${ProjectId}_version()) == 0) {
        fprintf(stderr, "expected a non-empty version string\n");
        return 1;
    }

    puts("${ProjectId}_tests: all tests passed");
    return 0;
}
"@
}

function Get-CdoSharedCMakeContent {
    param([string]$ProjectId)
    $buildMacro = $ProjectId.ToUpperInvariant() + '_BUILD'
    return @"
cmake_minimum_required(VERSION 3.20)
project($ProjectId VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

add_library($ProjectId SHARED
    src/$ProjectId.c
)
target_include_directories($ProjectId
    PUBLIC
        include
)
target_compile_definitions($ProjectId
    PRIVATE
        $buildMacro
)
cdo_apply_dependencies($ProjectId)
cdo_copy_runtime_dependencies($ProjectId)

if(CDO_ENABLE_TESTS)
    enable_testing()

    add_executable(${ProjectId}_tests
        tests/test_$ProjectId.c
    )
    target_link_libraries(${ProjectId}_tests
        PRIVATE
            $ProjectId
    )
    cdo_apply_dependencies(${ProjectId}_tests)
    cdo_copy_runtime_dependencies(${ProjectId}_tests)

    add_test(NAME ${ProjectId}_tests COMMAND ${ProjectId}_tests)
endif()
"@
}

function Get-CdoCliCppMainContent {
    param([string]$ProjectId)
    return @"
#include <iostream>

#include "$ProjectId/app.h"

int main(int argc, char **argv) {
    std::cout << ${ProjectId}_greeting() << std::endl;

    if (argc > 1) {
        std::cout << "Received " << (argc - 1) << " argument(s):" << std::endl;
        for (int i = 1; i < argc; ++i) {
            std::cout << "  " << i << ": " << argv[i] << std::endl;
        }
    }

    return 0;
}
"@
}

function Get-CdoCliCppAppContent {
    param([string]$ProjectId, [string]$ProjectName)
    return @"
#include "$ProjectId/app.h"

const char *${ProjectId}_greeting() {
    return "Hello from $ProjectName. Build something sharp.";
}

int ${ProjectId}_add(int left, int right) {
    return left + right;
}
"@
}

function Get-CdoCliCppHeaderContent {
    param([string]$ProjectId)
    $guard = Get-CdoHeaderGuard -ProjectId $ProjectId
    return @"
#ifndef $guard
#define $guard

const char *${ProjectId}_greeting();
int ${ProjectId}_add(int left, int right);

#endif
"@
}

function Get-CdoCliCppTestContent {
    param([string]$ProjectId)
    return @"
#include <iostream>
#include <cstring>

#include "$ProjectId/app.h"

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::cerr << "assertion failed: " << #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_INT(expected, actual) do { \
    int expected_value = (expected); \
    int actual_value = (actual); \
    if (expected_value != actual_value) { \
        std::cerr << "assertion failed: expected " << expected_value << ", got " << actual_value << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (0)

int main() {
    ASSERT_TRUE(${ProjectId}_greeting() != nullptr);
    ASSERT_TRUE(std::strlen(${ProjectId}_greeting()) > 0);
    ASSERT_EQ_INT(4, ${ProjectId}_add(2, 2));

    std::cout << "app_tests: all tests passed" << std::endl;
    return 0;
}
"@
}

function Get-CdoSkeletonDefinition {
    param(
        [string]$Skeleton,
        [string]$ProjectName,
        [string]$ProjectId
    )

    $name = Resolve-CdoSkeletonName -Name $Skeleton
    $directories = @('src', "include\$ProjectId", 'tests')
    $files = @()
    $defaultTarget = 'app'
    $runTarget = 'app'
    $requiredDependencies = @()
    $requiredTools = @()

    if ($name -eq 'cli') {
        $files = @(
            @{ Path = 'src\main.c'; Content = Get-CdoCliMainCContent -ProjectId $ProjectId },
            @{ Path = 'src\app.c'; Content = Get-CdoCliAppCContent -ProjectId $ProjectId -ProjectName $ProjectName },
            @{ Path = "include\$ProjectId\app.h"; Content = Get-CdoCliHeaderContent -ProjectId $ProjectId },
            @{ Path = 'tests\test_app.c'; Content = Get-CdoCliTestContent -ProjectId $ProjectId },
            @{ Path = 'CMakeLists.txt'; Content = Get-CdoExecutableCMakeContent -ProjectId $ProjectId }
        )
    } elseif ($name -eq 'sdl3') {
        $requiredDependencies = @('sdl3')
        $files = @(
            @{ Path = 'src\main.c'; Content = Get-CdoSdlMainCContent -ProjectId $ProjectId },
            @{ Path = 'src\app.c'; Content = Get-CdoSdlAppCContent -ProjectId $ProjectId -ProjectName $ProjectName },
            @{ Path = "include\$ProjectId\app.h"; Content = Get-CdoSdlHeaderContent -ProjectId $ProjectId },
            @{ Path = 'tests\test_app.c'; Content = Get-CdoCliTestContent -ProjectId $ProjectId },
            @{ Path = 'CMakeLists.txt'; Content = Get-CdoExecutableCMakeContent -ProjectId $ProjectId }
        )
    } elseif ($name -eq 'sdl3-gpu') {
        $requiredDependencies = @('sdl3')
        $requiredTools = @('dxc')
        $directories += @('assets\shaders\src', 'assets\shaders\compiled\dxil', 'assets\shaders\compiled\spirv')
        $files = @(
            @{ Path = 'src\main.c'; Content = Get-CdoSdlGpuMainCContent -ProjectId $ProjectId },
            @{ Path = 'src\gpu_app.c'; Content = Get-CdoSdlGpuSourceContent -ProjectId $ProjectId -ProjectName $ProjectName },
            @{ Path = 'src\shader_loader.c'; Content = Get-CdoShaderLoaderSourceContent -ProjectId $ProjectId },
            @{ Path = "include\$ProjectId\gpu_app.h"; Content = Get-CdoSdlGpuHeaderContent -ProjectId $ProjectId },
            @{ Path = "include\$ProjectId\shader_loader.h"; Content = Get-CdoShaderLoaderHeaderContent -ProjectId $ProjectId },
            @{ Path = 'tests\test_gpu_app.c'; Content = Get-CdoSdlGpuTestContent -ProjectId $ProjectId },
            @{ Path = 'assets\shaders\src\triangle.vert.hlsl'; Content = Get-CdoDefaultVertexShaderContent },
            @{ Path = 'assets\shaders\src\triangle.frag.hlsl'; Content = Get-CdoDefaultFragmentShaderContent },
            @{ Path = 'CMakeLists.txt'; Content = Get-CdoSdlGpuCMakeContent -ProjectId $ProjectId }
        )
    } elseif ($name -eq 'cli-cpp') {
        $files = @(
            @{ Path = 'src\main.cpp'; Content = Get-CdoCliCppMainContent -ProjectId $ProjectId },
            @{ Path = 'src\app.cpp'; Content = Get-CdoCliCppAppContent -ProjectId $ProjectId -ProjectName $ProjectName },
            @{ Path = "include\$ProjectId\app.h"; Content = Get-CdoCliCppHeaderContent -ProjectId $ProjectId },
            @{ Path = 'tests\test_app.cpp'; Content = Get-CdoCliCppTestContent -ProjectId $ProjectId },
            @{ Path = 'CMakeLists.txt'; Content = (Get-CdoMultiTargetCMakeContent -ProjectId $ProjectId -Targets @(
                @{ name = 'app'; type = 'executable'; target = 'app'; sources = @('src/main.cpp', 'src/app.cpp') },
                @{ name = 'app_tests'; type = 'test'; target = 'app_tests'; sources = @('tests/test_app.cpp') }
            ) -Config @{ cStandard = 17; cppStandard = 17 }) }
        )
    } elseif ($name -eq 'sdl3-cpp') {
        $requiredDependencies = @('sdl3')
        $files = @(
            @{ Path = 'src\main.cpp'; Content = Get-CdoSdlCppMainContent -ProjectId $ProjectId },
            @{ Path = 'src\app.cpp'; Content = Get-CdoSdlCppAppContent -ProjectId $ProjectId -ProjectName $ProjectName },
            @{ Path = "include\$ProjectId\app.h"; Content = Get-CdoSdlCppHeaderContent -ProjectId $ProjectId },
            @{ Path = 'tests\test_app.cpp'; Content = Get-CdoSdlCppTestContent -ProjectId $ProjectId },
            @{ Path = 'CMakeLists.txt'; Content = (Get-CdoMultiTargetCMakeContent -ProjectId $ProjectId -Targets @(
                @{ name = 'app'; type = 'executable'; target = 'app'; sources = @('src/main.cpp', 'src/app.cpp') },
                @{ name = 'app_tests'; type = 'test'; target = 'app_tests'; sources = @('tests/test_app.cpp') }
            ) -Config @{ cStandard = 17; cppStandard = 17 }) }
        )
    } elseif ($name -eq 'sdl3-gpu-cpp') {
        $requiredDependencies = @('sdl3')
        $requiredTools = @('dxc')
        $directories += @('assets\shaders\src', 'assets\shaders\compiled\dxil', 'assets\shaders\compiled\spirv')
        $files = @(
            @{ Path = 'src\main.cpp'; Content = Get-CdoSdlGpuCppMainContent -ProjectId $ProjectId },
            @{ Path = 'src\gpu_app.cpp'; Content = Get-CdoSdlGpuSourceContent -ProjectId $ProjectId -ProjectName $ProjectName },
            @{ Path = 'src\shader_loader.cpp'; Content = Get-CdoShaderLoaderCppSourceContent -ProjectId $ProjectId },
            @{ Path = "include\$ProjectId\gpu_app.h"; Content = Get-CdoSdlGpuCppHeaderContent -ProjectId $ProjectId },
            @{ Path = "include\$ProjectId\shader_loader.h"; Content = Get-CdoShaderLoaderCppHeaderContent -ProjectId $ProjectId },
            @{ Path = 'tests\test_gpu_app.cpp'; Content = Get-CdoSdlGpuCppTestContent -ProjectId $ProjectId },
            @{ Path = 'assets\shaders\src\triangle.vert.hlsl'; Content = Get-CdoDefaultVertexShaderContent },
            @{ Path = 'assets\shaders\src\triangle.frag.hlsl'; Content = Get-CdoDefaultFragmentShaderContent },
            @{ Path = 'CMakeLists.txt'; Content = (Get-CdoMultiTargetCMakeContent -ProjectId $ProjectId -Targets @(
                @{ name = 'app'; type = 'executable'; target = 'app'; sources = @('src/main.cpp', 'src/gpu_app.cpp', 'src/shader_loader.cpp') },
                @{ name = 'app_tests'; type = 'test'; target = 'app_tests'; sources = @('tests/test_gpu_app.cpp') }
            ) -Config @{ cStandard = 17; cppStandard = 17 }) }
        )
    } else {
        $defaultTarget = 'lib'
        $runTarget = ''
        $files = @(
            @{ Path = "src\$ProjectId.c"; Content = Get-CdoSharedSourceContent -ProjectId $ProjectId },
            @{ Path = "include\$ProjectId\$ProjectId.h"; Content = Get-CdoSharedHeaderContent -ProjectId $ProjectId },
            @{ Path = "tests\test_$ProjectId.c"; Content = Get-CdoSharedTestContent -ProjectId $ProjectId },
            @{ Path = 'CMakeLists.txt'; Content = Get-CdoSharedCMakeContent -ProjectId $ProjectId }
        )
    }

    return [pscustomobject]@{
        Name = $name
        Description = Get-CdoSkeletonDescription -Name $name
        Manifest = New-CdoSkeletonManifest -ProjectName $ProjectName -ProjectId $ProjectId -Skeleton $name
        Directories = $directories
        Files = $files
        Recipe = New-CdoSkeletonRecipe -Directories $directories -Files $files
        DefaultTarget = $defaultTarget
        RunTarget = $runTarget
        RequiredDependencies = $requiredDependencies
        RequiredTools = $requiredTools
    }
}

function New-CdoSkeletonRecipe {
    param(
        [object[]]$Directories = @(),
        [object[]]$Files = @()
    )

    $ops = @()
    foreach ($dir in $Directories) {
        $ops += [ordered]@{
            kind = 'directory'
            path = [string]$dir
        }
    }
    foreach ($file in $Files) {
        $ops += [ordered]@{
            kind = 'file'
            path = [string]$file.Path
            content = [string]$file.Content
            mode = 'if-missing'
        }
    }
    return $ops
}

function Invoke-CdoSkeletonRecipe {
    param(
        [string]$Root,
        [object[]]$Recipe,
        [switch]$Force
    )

    $results = @()
    foreach ($op in $Recipe) {
        $kind = Get-CdoProperty -Object $op -Name 'kind'
        $path = Get-CdoProperty -Object $op -Name 'path'
        $target = Join-Path $Root $path

        if ($kind -eq 'directory') {
            New-CdoDirectory -Path $target
            $results += [pscustomobject]@{ Kind = 'directory'; Path = $path; Action = 'ensured' }
        } elseif ($kind -eq 'file') {
            $content = [string](Get-CdoProperty -Object $op -Name 'content' -Default '')
            if ($Force) {
                Write-CdoTextFile -Path $target -Content $content
                Write-CdoLog -Level OK -Message "wrote $target"
                $results += [pscustomobject]@{ Kind = 'file'; Path = $path; Action = 'wrote' }
            } elseif (Test-Path -LiteralPath $target) {
                Write-CdoLog -Level INFO -Message "kept existing $target"
                $results += [pscustomobject]@{ Kind = 'file'; Path = $path; Action = 'kept' }
            } else {
                Write-CdoTextFile -Path $target -Content $content
                Write-CdoLog -Level OK -Message "created $target"
                $results += [pscustomobject]@{ Kind = 'file'; Path = $path; Action = 'created' }
            }
        } else {
            throw "Unknown skeleton recipe operation '$kind' for $path"
        }
    }
    return $results
}

function Get-CdoMultiTargetCMakeContent {
    param(
        [string]$ProjectId,
        [object[]]$Targets,
        [object]$Config
    )

    $cStandard = Get-CdoProperty -Object $Config -Name 'cStandard' -Default 17
    $cppStandard = Get-CdoProperty -Object $Config -Name 'cppStandard'

    $languages = "C"
    if ($null -ne $cppStandard) {
        $languages = "C CXX"
    }

    $lines = @()
    $lines += "cmake_minimum_required(VERSION 3.20)"
    $lines += "project($ProjectId VERSION 0.1.0 LANGUAGES $languages)"
    $lines += ""
    $lines += "set(CMAKE_C_STANDARD $cStandard)"
    $lines += "set(CMAKE_C_STANDARD_REQUIRED ON)"
    $lines += "set(CMAKE_C_EXTENSIONS OFF)"
    $lines += ""

    if ($null -ne $cppStandard) {
        $lines += "set(CMAKE_CXX_STANDARD $cppStandard)"
        $lines += "set(CMAKE_CXX_STANDARD_REQUIRED ON)"
        $lines += "set(CMAKE_CXX_EXTENSIONS OFF)"
        $lines += ""
    }

    $lines += 'option(CDO_ENABLE_TESTS "Build cdo-generated tests" ON)'
    $lines += ""
    $lines += "include(cmake/cdo_deps.cmake OPTIONAL)"
    $lines += ""
    $lines += "if(NOT COMMAND cdo_apply_dependencies)"
    $lines += "    function(cdo_apply_dependencies target)"
    $lines += "    endfunction()"
    $lines += "endif()"
    $lines += ""
    $lines += "if(NOT COMMAND cdo_copy_runtime_dependencies)"
    $lines += "    function(cdo_copy_runtime_dependencies target)"
    $lines += "    endfunction()"
    $lines += "endif()"
    $lines += ""

    # Non-test targets
    $nonTestTargets = @($Targets | Where-Object {
        (Get-CdoProperty -Object $_ -Name 'type') -ne 'test'
    })

    foreach ($t in $nonTestTargets) {
        $name = Get-CdoProperty -Object $t -Name 'name'
        $type = Get-CdoProperty -Object $t -Name 'type'
        $target = Get-CdoProperty -Object $t -Name 'target' -Default $name
        $sources = @(Get-CdoProperty -Object $t -Name 'sources' -Default @())

        switch ($type) {
            'executable' {
                $lines += "add_executable($target"
                foreach ($src in $sources) { $lines += "    $src" }
                $lines += ")"
            }
            'library' {
                $lines += "add_library($target STATIC"
                foreach ($src in $sources) { $lines += "    $src" }
                $lines += ")"
            }
            'shared-library' {
                $lines += "add_library($target SHARED"
                foreach ($src in $sources) { $lines += "    $src" }
                $lines += ")"
            }
        }

        $lines += "cdo_apply_dependencies($target)"
        $lines += "cdo_copy_runtime_dependencies($target)"
        $lines += ""
    }

    # Test targets
    $testTargets = @($Targets | Where-Object {
        (Get-CdoProperty -Object $_ -Name 'type') -eq 'test'
    })

    if ($testTargets.Count -gt 0) {
        $lines += "if(CDO_ENABLE_TESTS)"
        $lines += "    enable_testing()"
        $lines += ""

        foreach ($t in $testTargets) {
            $name = Get-CdoProperty -Object $t -Name 'name'
            $target = Get-CdoProperty -Object $t -Name 'target' -Default $name
            $sources = @(Get-CdoProperty -Object $t -Name 'sources' -Default @())

            $lines += "    add_executable($target"
            foreach ($src in $sources) { $lines += "        $src" }
            $lines += "    )"
            $lines += "    cdo_apply_dependencies($target)"
            $lines += "    cdo_copy_runtime_dependencies($target)"
            $lines += ""
            $lines += "    add_test(NAME $target COMMAND $target)"
            $lines += ""
        }

        $lines += "endif()"
        $lines += ""
    }

    return ($lines -join "`n")
}

# ============================================================================
# Per-Target Skeleton Functions
# ============================================================================
# These functions provide skeleton file content for `cdo target create`.
# Skeleton parameter selects the language variant:
#   - "cpp" (default) — C++ source with .cpp extension
#   - "c" — C source with .c extension
# ============================================================================

function Resolve-CdoTargetSkeletonName {
    param([string]$Name)

    if ([string]::IsNullOrWhiteSpace($Name)) { return 'cpp' }

    $key = $Name.ToLowerInvariant()
    switch ($key) {
        'cpp'   { return 'cpp' }
        'c++'   { return 'cpp' }
        'cxx'   { return 'cpp' }
        'c'     { return 'c' }
        default {
            throw "Unknown target skeleton '$Name'. Available target skeletons: cpp, c"
        }
    }
}

function Get-CdoTargetSkeletonNames {
    return @('cpp', 'c')
}

function Get-CdoTargetSkeletonSourceExtension {
    param([string]$Skeleton)

    $resolved = Resolve-CdoTargetSkeletonName -Name $Skeleton
    switch ($resolved) {
        'cpp' { return '.cpp' }
        'c'   { return '.c' }
    }
}

function Get-CdoTargetSkeletonHeaderExtension {
    param([string]$Skeleton)
    # Headers are always .h regardless of skeleton
    return '.h'
}

function Get-CdoTargetSkeletonSource {
    param(
        [string]$Name,
        [string]$Type,           # "executable" or "library"
        [string]$Skeleton        # skeleton name (optional, defaults to cpp)
    )

    $resolved = Resolve-CdoTargetSkeletonName -Name $Skeleton

    if ($Type -eq 'executable') {
        if ($resolved -eq 'c') {
            return @"
#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("Hello from $Name!\n");
    return 0;
}
"@
        } else {
            # cpp (default)
            return @"
#include <cstdio>

int main(int argc, char *argv[]) {
    std::printf("Hello from $Name!\n");
    return 0;
}
"@
        }
    } elseif ($Type -eq 'library') {
        if ($resolved -eq 'c') {
            return @"
#include "$Name/$Name.h"
"@
        } else {
            # cpp (default)
            return @"
#include "$Name/$Name.h"

namespace $($Name.Replace('-', '_')) {

} // namespace $($Name.Replace('-', '_'))
"@
        }
    }

    throw "Unknown target type '$Type' for skeleton source generation."
}

function Get-CdoTargetSkeletonHeader {
    param(
        [string]$Name,
        [string]$Type           # "executable" or "library"
    )

    # Headers are only generated for library targets
    if ($Type -ne 'library') {
        return $null
    }

    $nameUpper = $Name.ToUpperInvariant().Replace('-', '_')
    return @"
#ifndef ${nameUpper}_H
#define ${nameUpper}_H

// Public API for $Name

#endif // ${nameUpper}_H
"@
}

function Get-CdoTargetSkeletonTest {
    param(
        [string]$Name,
        [string]$Type,           # "executable" or "library"
        [string]$Skeleton        # skeleton name (optional, defaults to cpp)
    )

    $resolved = Resolve-CdoTargetSkeletonName -Name $Skeleton

    if ($Type -eq 'executable') {
        if ($resolved -eq 'c') {
            return @"
#include <stdio.h>

int main(void) {
    printf("Tests for $Name\n");
    return 0;
}
"@
        } else {
            # cpp (default)
            return @"
#include <cstdio>

int main() {
    std::printf("Tests for $Name\n");
    return 0;
}
"@
        }
    } elseif ($Type -eq 'library') {
        if ($resolved -eq 'c') {
            return @"
#include "$Name/$Name.h"
#include <stdio.h>

int main(void) {
    printf("Tests for $Name\n");
    return 0;
}
"@
        } else {
            # cpp (default)
            return @"
#include "$Name/$Name.h"
#include <cstdio>

int main() {
    std::printf("Tests for $Name\n");
    return 0;
}
"@
        }
    }

    throw "Unknown target type '$Type' for skeleton test generation."
}

function Get-CdoTargetSkeletonSourceFileName {
    param(
        [string]$Name,
        [string]$Type,           # "executable" or "library"
        [string]$Skeleton        # skeleton name (optional, defaults to cpp)
    )

    $ext = Get-CdoTargetSkeletonSourceExtension -Skeleton $Skeleton

    if ($Type -eq 'executable') {
        return "main$ext"
    } elseif ($Type -eq 'library') {
        return "$Name$ext"
    }

    throw "Unknown target type '$Type' for skeleton source file name."
}

function Get-CdoTargetSkeletonTestFileName {
    param(
        [string]$Name,
        [string]$Type,           # "executable" or "library"
        [string]$Skeleton        # skeleton name (optional, defaults to cpp)
    )

    $ext = Get-CdoTargetSkeletonSourceExtension -Skeleton $Skeleton
    return "test_$Name$ext"
}

function Get-CdoTargetSkeletonHeaderFileName {
    param(
        [string]$Name
    )

    return "$Name.h"
}
