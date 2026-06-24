<#
cdo - C Development Ops for portable Windows-friendly C projects.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir 'CdoHelpers.ps1')
. (Join-Path $ScriptDir 'CdoSkeletons.ps1')
. (Join-Path $ScriptDir 'CdoTools.ps1')
. (Join-Path $ScriptDir 'CdoShaders.ps1')
. (Join-Path $ScriptDir 'CdoSources.ps1')
. (Join-Path $ScriptDir 'CdoTargets.ps1')

$script:CdoVersion = '0.4.0'
$Global:CdoDebug = $false

function Show-Help {
    Write-Host "cdo $script:CdoVersion - C Development Ops"
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  cdo <command> [options]"
    Write-Host ""
    Write-Host "Project commands:"
    Write-Host "  init [path] [--name <name>] [--skeleton <cli|cli-cpp|shared-lib|sdl3|sdl3-cpp|sdl3-gpu|sdl3-gpu-cpp>] [--dry-run] [--force] [--no-self] [--install-tools]"
    Write-Host "  doctor [--fix]              Check compiler, CMake, Git, and project wiring"
    Write-Host "  activate                    Show activation command for this shell"
    Write-Host "  sync [--sources] [--shaders] [--all]  Regenerate .cdo/CMake glue and optional file manifests"
    Write-Host "  self <command>              Inspect or upgrade the vendored project-local CDo"
    Write-Host "  skeletons                   List init skeletons"
    Write-Host "  skeleton apply <name>       Apply or upgrade skeleton recipe in a project"
    Write-Host "  target list                 List all declared targets"
    Write-Host "  target create <name> --type <type> [--static|--shared]  Create a new target"
    Write-Host "  target delete <name>        Remove a target from the manifest"
    Write-Host "  source <command>            List/include/exclude C source files in the build"
    Write-Host "  tool <command>              Manage portable project tools such as dxc"
    Write-Host "  shader <command>            Manage shader sources and compiled outputs"
    Write-Host ""
    Write-Host "Dependency commands:"
    Write-Host "  add <name> [--archive <zip/tar>] [--force] [--refresh-cache]"
    Write-Host "  remove <name> [--delete-files]"
    Write-Host "  list                        List catalog and active dependencies"
    Write-Host ""
    Write-Host "Build commands:"
    Write-Host "  build [target] [--release] [--config <Debug|Release|...>] [--generator <name>] [--no-shaders]"
    Write-Host "  run [target] [args...] [--release] [--config <Debug|Release|...>]"
    Write-Host "  test [target] [--release] [--config <Debug|Release|...>]"
    Write-Host "  clean"
    Write-Host ""
    Write-Host "Shader commands:"
    Write-Host "  shader add <vertex|fragment|compute> <name> [--force]"
    Write-Host "  shader list"
    Write-Host "  shader include <file...> [--pattern <glob>] [--all] [--dry-run]"
    Write-Host "  shader exclude <file...> [--pattern <glob>] [--all] [--dry-run]"
    Write-Host "  shader cleanup [--dry-run]"
    Write-Host "  shader sync [--pattern <glob>] [--dry-run]"
    Write-Host "  shader doctor"
    Write-Host "  shader compile [--dry-run]"
    Write-Host "  shader clean                  Clean compiled shader outputs"
    Write-Host ""
    Write-Host "Source commands:"
    Write-Host "  source list [--target <cmake-target>] [--pattern <glob>]"
    Write-Host "  source include <file...> [--target <cmake-target>] [--pattern <glob>] [--all] [--dry-run]"
    Write-Host "  source exclude <file...> [--target <cmake-target>] [--pattern <glob>] [--all] [--dry-run]"
    Write-Host "  source cleanup [--target <cmake-target>] [--dry-run]"
    Write-Host "  source sync [--target <cmake-target>] [--pattern <glob>] [--dry-run]"
    Write-Host ""
    Write-Host "Tool commands:"
    Write-Host "  tool list"
    Write-Host "  tool doctor"
    Write-Host "  tool install <name> [--version <tag|latest-stable>] [--force] [--dry-run] [--refresh-cache]"
    Write-Host ""
    Write-Host "Self commands:"
    Write-Host "  version [--all]"
    Write-Host "  self status [--from <path>] [--to <project-path>]"
    Write-Host "  self upgrade [--from <path>] [--to <project-path>]"
    Write-Host "  cache path"
    Write-Host ""
    Write-Host "Global options:"
    Write-Host "  --project <path>            Use a project root explicitly"
    Write-Host "  --debug                     Print debug logs"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  cdo init . --name hello_cli"
    Write-Host "  cdo init . --name hello_sdl --skeleton sdl3"
    Write-Host "  cdo init . --name shader_lab --skeleton sdl3-gpu"
    Write-Host "  . .\.cdo\activate.ps1"
    Write-Host "  cdo add sdl3"
    Write-Host "  cdo tool install dxc"
    Write-Host "  cdo build app --release"
    Write-Host "  cdo run app"
    Write-Host "  cdo test app_tests"
}

function Show-Skeletons {
    Write-Host "Available init skeletons:"
    foreach ($name in (Get-CdoSkeletonNames)) {
        Write-Host ("  {0,-12} {1}" -f $name, (Get-CdoSkeletonDescription -Name $name))
    }
}

function Show-ShaderHelp {
    Write-Host "Shader commands:"
    Write-Host "  cdo shader add <vertex|fragment|compute> <name> [--force]"
    Write-Host "  cdo shader list"
    Write-Host "  cdo shader include <file...> [--pattern <glob>] [--all] [--dry-run]"
    Write-Host "  cdo shader exclude <file...> [--pattern <glob>] [--all] [--dry-run]"
    Write-Host "  cdo shader cleanup [--dry-run]"
    Write-Host "  cdo shader sync [--pattern <glob>] [--dry-run]"
    Write-Host "  cdo shader doctor"
    Write-Host "  cdo shader compile [--dry-run]"
    Write-Host "  cdo shader clean                  Clean compiled shader outputs"
}

function Show-ToolHelp {
    Write-Host "Tool commands:"
    Write-Host "  cdo tool list"
    Write-Host "  cdo tool doctor"
    Write-Host "  cdo tool install <name> [--version <tag|latest-stable>] [--force] [--dry-run] [--refresh-cache]"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  cdo tool install dxc"
    Write-Host "  cdo tool install dxc --version latest-stable"
}

function Show-SelfHelp {
    Write-Host "Self commands:"
    Write-Host "  cdo version [--all]"
    Write-Host "  cdo self status [--from <path>] [--to <project-path>]"
    Write-Host "  cdo self upgrade [--from <path>] [--to <project-path>]"
    Write-Host "  cdo cache path"
}

function Get-CdoScriptVersion {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path)) { return '' }
    $match = Select-String -LiteralPath $Path -Pattern "\`$script:CdoVersion\s*=\s*'([^']+)'" -AllMatches | Select-Object -First 1
    if ($match -and $match.Matches.Count -gt 0) { return $match.Matches[0].Groups[1].Value }
    return ''
}

function Test-CdoVendoredScriptDir {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    $normalized = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    return ($normalized -match '\\\.cdo\\bin$')
}

function Resolve-CdoSourceDirectory {
    param([string]$From = '', [string]$Root = '')

    if (-not [string]::IsNullOrWhiteSpace($From)) {
        $resolved = Resolve-CdoPath -Path $From
        if (Test-Path -LiteralPath $resolved -PathType Leaf) { return Split-Path -Parent $resolved }
        return $resolved
    }

    if (-not (Test-CdoVendoredScriptDir -Path $ScriptDir)) {
        return $ScriptDir
    }

    $localBin = if ($Root) { [System.IO.Path]::GetFullPath((Join-Path $Root '.cdo\bin')) } else { '' }
    foreach ($commandName in @('cdo.ps1', 'cdo.cmd', 'cdo')) {
        foreach ($cmd in @(Get-Command $commandName -ErrorAction SilentlyContinue)) {
            $source = [string]$cmd.Source
            if ([string]::IsNullOrWhiteSpace($source)) { continue }
            if ($source.EndsWith('.cmd', [System.StringComparison]::OrdinalIgnoreCase)) {
                $source = Join-Path (Split-Path -Parent $source) 'cdo.ps1'
            }
            if (-not (Test-Path -LiteralPath $source)) { continue }
            $dir = [System.IO.Path]::GetFullPath((Split-Path -Parent $source))
            if ($localBin -and $dir.Equals($localBin, [System.StringComparison]::OrdinalIgnoreCase)) { continue }
            if (-not (Test-CdoVendoredScriptDir -Path $dir)) { return $dir }
        }
    }

    return ''
}

function Resolve-CdoSelfTargetRoot {
    param([object]$Parsed)

    $to = Get-ParsedOption -Parsed $Parsed -Name 'to' -Default ''
    if ([string]::IsNullOrWhiteSpace($to)) {
        return Get-ProjectRootForCli -Parsed $Parsed
    }

    $resolved = Resolve-CdoPath -Path $to
    if (Test-Path -LiteralPath $resolved -PathType Leaf) {
        $leaf = Split-Path -Leaf $resolved
        if ($leaf -ieq 'cdo.ps1' -or $leaf -ieq 'cdo.cmd' -or $leaf -ieq 'cdo') {
            $resolved = Split-Path -Parent $resolved
        } else {
            throw "--to must point to a project directory, .cdo, .cdo\bin, or project-local cdo.ps1."
        }
    }

    $full = [System.IO.Path]::GetFullPath($resolved).TrimEnd('\', '/')
    $leafDir = Split-Path -Leaf $full
    if ($leafDir -ieq 'bin' -and ((Split-Path -Leaf (Split-Path -Parent $full)) -ieq '.cdo')) {
        $full = Split-Path -Parent (Split-Path -Parent $full)
    } elseif ($leafDir -ieq '.cdo') {
        $full = Split-Path -Parent $full
    }

    return $full
}

function Show-CdoVersionStatus {
    param([string]$Root = '', [string]$From = '')

    Write-Host "Running:"
    Write-Host ("  {0,-10} {1}" -f $script:CdoVersion, (Join-Path $ScriptDir 'cdo.ps1'))

    $localVersion = ''
    if ($Root -and (Test-Path -LiteralPath (Join-Path $Root 'project.cdo.json'))) {
        $local = Join-Path $Root '.cdo\bin\cdo.ps1'
        $localVersion = Get-CdoScriptVersion -Path $local
        Write-Host ""
        Write-Host "Project-local:"
        if ($localVersion) {
            Write-Host ("  {0,-10} {1}" -f $localVersion, $local)
        } else {
            Write-Host "  (missing)"
        }
    }

    $sourceDir = Resolve-CdoSourceDirectory -From $From -Root $Root
    $sourceVersion = ''
    Write-Host ""
    if ([string]::IsNullOrWhiteSpace($From)) {
        Write-Host "Global/source:"
    } else {
        Write-Host "Upgrade source:"
    }
    if ($sourceDir) {
        $sourceScript = Join-Path $sourceDir 'cdo.ps1'
        if (Test-Path -LiteralPath $sourceScript) {
            $sourceVersion = Get-CdoScriptVersion -Path $sourceScript
            Write-Host ("  {0,-10} {1}" -f $sourceVersion, $sourceScript)
        } else {
            Write-Host "  (missing cdo.ps1 at $sourceDir)"
        }
    } else {
        Write-Host "  (not found; pass --from <path> to self status or self upgrade)"
    }

    if ($localVersion -and $sourceVersion) {
        Write-Host ""
        Write-Host "Project-local status:"
        if ($localVersion -eq $sourceVersion) {
            Write-Host "  current"
        } else {
            Write-Host ("  differs from source ({0} -> {1})" -f $localVersion, $sourceVersion)
        }
    }

    Write-Host ""
    Write-Host "User cache:"
    Write-Host "  $(Get-CdoGlobalCacheRoot)"
}

function Invoke-CdoSelfUpgrade {
    param([string]$Root, [string]$From = '')

    $sourceDir = Resolve-CdoSourceDirectory -From $From -Root $Root
    if ([string]::IsNullOrWhiteSpace($sourceDir)) {
        throw "Could not find a global/source CDo. Pass --from <path-to-cdo-folder-or-cdo.ps1>."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $sourceDir 'cdo.ps1'))) {
        throw "No cdo.ps1 found in source directory: $sourceDir"
    }

    Install-CdoSelf -Root $Root -SourceDir $sourceDir
    $local = Join-Path $Root '.cdo\bin\cdo.ps1'
    Write-CdoLog -Level OK -Message "project-local CDo is now $(Get-CdoScriptVersion -Path $local)"
}

function Apply-CdoSkeletonToProject {
    param(
        [string]$Root,
        [string]$Skeleton,
        [switch]$Force
    )

    $config = Load-Config -Root $Root
    $projectName = [string](Get-CdoProperty -Object $config -Name 'name' -Default (Split-Path $Root -Leaf))
    $projectId = [string](Get-CdoProperty -Object $config -Name 'id' -Default (Get-CdoSafeName -Name $projectName))
    $definition = Get-CdoSkeletonDefinition -Skeleton $Skeleton -ProjectName $projectName -ProjectId $projectId

    Write-CdoLog -Level INFO -Message "applying $($definition.Name) skeleton recipe"
    Write-ProjectSkeleton -Root $Root -ProjectName $projectName -ProjectId $projectId -Skeleton $definition.Name -Force:$Force

    Set-CdoProperty -Object $config -Name 'skeleton' -Value $definition.Name
    Set-CdoProperty -Object $config -Name 'requiredDependencies' -Value @($definition.RequiredDependencies)
    Set-CdoProperty -Object $config -Name 'requiredTools' -Value @($definition.RequiredTools)

    $fresh = New-CdoManifest -ProjectName $projectName -ProjectId $projectId -Skeleton $definition.Name
    $cppSkeletons = @('cli-cpp', 'sdl3-cpp', 'sdl3-gpu-cpp')
    if ($cppSkeletons -contains $definition.Name) {
        # C++ skeletons use targets array exclusively; remove legacy fields
        if ($config -is [System.Collections.IDictionary]) {
            $config.Remove('apps')
            $config.Remove('tests')
        } else {
            $config.PSObject.Properties.Remove('apps')
            $config.PSObject.Properties.Remove('tests')
        }
        Set-CdoProperty -Object $config -Name 'targets' -Value (Get-CdoProperty -Object $fresh -Name 'targets')
        Set-CdoProperty -Object $config -Name 'cppStandard' -Value (Get-CdoProperty -Object $fresh -Name 'cppStandard')
        if ($definition.Name -eq 'sdl3-gpu-cpp') {
            Set-CdoProperty -Object $config -Name 'shaderToolchain' -Value (Get-CdoProperty -Object $fresh -Name 'shaderToolchain')
            Set-CdoProperty -Object $config -Name 'shaders' -Value (Get-CdoProperty -Object $fresh -Name 'shaders')
        }
    } else {
        Set-CdoProperty -Object $config -Name 'apps' -Value (Get-CdoProperty -Object $fresh -Name 'apps')
        Set-CdoProperty -Object $config -Name 'tests' -Value (Get-CdoProperty -Object $fresh -Name 'tests')
        if ($definition.Name -eq 'sdl3-gpu') {
            Set-CdoProperty -Object $config -Name 'shaderToolchain' -Value (Get-CdoProperty -Object $fresh -Name 'shaderToolchain')
            Set-CdoProperty -Object $config -Name 'shaders' -Value (Get-CdoProperty -Object $fresh -Name 'shaders')
        }
    }

    Save-Config -Config $config -Root $Root
    Write-CmakeDependencies -Root $Root -Config $config
    Write-CdoLog -Level OK -Message "skeleton recipe applied"
}

function Parse-CdoCliArgs {
    param([string[]]$RawArgs)

    $booleanFlags = @{}
    foreach ($flag in @('debug', 'verbose', 'dry-run', 'force', 'release', 'no-self', 'install-tools', 'no-shaders', 'refresh-cache', 'delete-files', 'fix', 'help', 'all', 'sources', 'shaders', 'list-skeletons', 'static', 'shared')) {
        $booleanFlags[$flag] = $true
    }

    $positionals = New-Object 'System.Collections.Generic.List[string]'
    $options = @{}
    $flags = @{}

    for ($i = 0; $i -lt $RawArgs.Count; $i++) {
        $arg = $RawArgs[$i]
        if ($arg -like '--*') {
            $name = $arg.Substring(2)
            if ($booleanFlags.ContainsKey($name)) {
                $flags[$name] = $true
            } else {
                if ($i + 1 -ge $RawArgs.Count) {
                    throw "Option --$name requires a value."
                }
                $options[$name] = $RawArgs[$i + 1]
                $i++
            }
        } elseif ($arg -eq '-h') {
            $flags['help'] = $true
        } elseif ($arg -eq '-v') {
            $flags['verbose'] = $true
        } else {
            $positionals.Add($arg)
        }
    }

    return [pscustomobject]@{
        Positionals = $positionals.ToArray()
        Options = $options
        Flags = $flags
    }
}

function Get-ParsedFlag {
    param([object]$Parsed, [string]$Name)
    return $Parsed.Flags.ContainsKey($Name)
}

function Get-ParsedOption {
    param([object]$Parsed, [string]$Name, [object]$Default = $null)
    if ($Parsed.Options.ContainsKey($Name)) { return $Parsed.Options[$Name] }
    return $Default
}

function Get-CommandPositionals {
    param([object]$Parsed)
    $all = @($Parsed.Positionals)
    if ($all.Count -le 1) { return @() }
    return @($all[1..($all.Count - 1)])
}

function Get-ProjectRootForCli {
    param([object]$Parsed)
    $explicit = Get-ParsedOption -Parsed $Parsed -Name 'project'
    if ($explicit) { return Resolve-CdoPath -Path $explicit }
    return Get-CdoProjectRoot
}

function Get-Catalog {
    $catFile = Join-Path $ScriptDir 'catalog.json'
    if (-not (Test-Path -LiteralPath $catFile)) {
        throw "catalog.json not found next to cdo.ps1 ($ScriptDir)."
    }
    return Read-CdoJson -Path $catFile
}

function Get-CatalogNames {
    $catalog = Get-Catalog
    return @($catalog.PSObject.Properties | ForEach-Object { $_.Name } | Sort-Object)
}

function Resolve-CatalogName {
    param([string]$Name)
    $catalog = Get-Catalog
    foreach ($prop in $catalog.PSObject.Properties) {
        if ($prop.Name -ieq $Name) { return $prop.Name }
    }
    throw "Dependency '$Name' is not in the catalog. Run 'cdo list' to see available packages."
}

function Get-CatalogEntry {
    param([string]$Name)
    $key = Resolve-CatalogName -Name $Name
    $catalog = Get-Catalog
    return [pscustomobject]@{
        Key = $key
        Entry = Get-CdoProperty -Object $catalog -Name $key
    }
}

function Load-Config {
    param([string]$Root)
    $path = Join-Path $Root 'project.cdo.json'
    $cfg = Read-CdoJson -Path $path
    if ($null -eq $cfg) { throw "No project.cdo.json found at $Root. Run 'cdo init .' first." }
    return $cfg
}

function Save-Config {
    param([object]$Config, [string]$Root)
    Write-CdoJson -Path (Join-Path $Root 'project.cdo.json') -Value $Config
}

function Get-DependencyMap {
    param([object]$Config)
    $deps = Get-CdoProperty -Object $Config -Name 'dependencies'
    if ($null -eq $deps) { $deps = Get-CdoProperty -Object $Config -Name 'deps' }
    $map = Convert-CdoObjectToHashtable -InputObject $deps
    if ($null -eq $map) { $map = [ordered]@{} }
    return $map
}

function Set-DependencyMap {
    param([object]$Config, [object]$Dependencies)
    Set-CdoProperty -Object $Config -Name 'dependencies' -Value $Dependencies
    $legacy = Get-CdoProperty -Object $Config -Name 'deps'
    if ($null -ne $legacy) { Set-CdoProperty -Object $Config -Name 'deps' -Value $null }
}

function Get-Apps {
    param([object]$Config)
    return @(Get-CdoProperty -Object $Config -Name 'apps' -Default @())
}

function Get-DefaultAppName {
    param([object]$Config)
    $apps = Get-Apps -Config $Config
    if ($apps.Count -eq 0) { return 'app' }
    return [string](Get-CdoProperty -Object $apps[0] -Name 'name' -Default (Get-CdoProperty -Object $apps[0] -Name 'target' -Default 'app'))
}

function Get-AppConfig {
    param([object]$Config, [string]$Name)
    if ([string]::IsNullOrWhiteSpace($Name)) { $Name = Get-DefaultAppName -Config $Config }
    foreach ($app in (Get-Apps -Config $Config)) {
        $appName = [string](Get-CdoProperty -Object $app -Name 'name' -Default '')
        $target = [string](Get-CdoProperty -Object $app -Name 'target' -Default $appName)
        if ($appName -ieq $Name -or $target -ieq $Name) { return $app }
    }
    throw "App '$Name' is not declared in project.cdo.json."
}

function Get-AppTarget {
    param([object]$App)
    return [string](Get-CdoProperty -Object $App -Name 'target' -Default (Get-CdoProperty -Object $App -Name 'name' -Default 'app'))
}

function Get-BuildDirectory {
    param([object]$Config, [string]$Root)
    $build = Get-CdoProperty -Object $Config -Name 'build'
    $dir = [string](Get-CdoProperty -Object $build -Name 'directory' -Default 'build')
    return Join-Path $Root $dir
}

function Get-BuildConfig {
    param([object]$Parsed, [object]$Config)
    $explicit = Get-ParsedOption -Parsed $Parsed -Name 'config'
    if ($explicit) { return [string]$explicit }
    if (Get-ParsedFlag -Parsed $Parsed -Name 'release') { return 'Release' }
    $build = Get-CdoProperty -Object $Config -Name 'build'
    return [string](Get-CdoProperty -Object $build -Name 'defaultConfig' -Default 'Debug')
}

function New-CdoManifest {
    param([string]$ProjectName, [string]$ProjectId, [string]$Skeleton = 'cli')
    $definition = Get-CdoSkeletonDefinition -Skeleton $Skeleton -ProjectName $ProjectName -ProjectId $ProjectId
    return $definition.Manifest
}

function Get-HeaderGuard {
    param([string]$ProjectId)
    return ($ProjectId.ToUpperInvariant() + '_APP_H')
}

function Get-MainCContent {
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

function Get-AppCContent {
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

function Get-AppHeaderContent {
    param([string]$ProjectId)
    $guard = Get-HeaderGuard -ProjectId $ProjectId
    return @"
#ifndef $guard
#define $guard

const char *${ProjectId}_greeting(void);
int ${ProjectId}_add(int left, int right);

#endif
"@
}

function Get-TestContent {
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

function Get-CMakeListsContent {
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

function Get-GitignoreContent {
    return @'
# Build outputs
build/
out/
dist/
*.exe
*.dll
*.lib
*.obj
*.pdb
*.ilk

# cdo caches and portable tools
.cdo/cache/
.cdo/tools/

# Dependency archives, but not vendored dependency source/binaries
third_party/**/*.zip
third_party/**/*.tar
third_party/**/*.tar.gz
third_party/**/*.tgz
third_party/**/*.tar.bz2
third_party/**/*.tbz2

# Editors and local state
.vs/
*.user
*.suo
.DS_Store
'@
}

function Get-EditorConfigContent {
    return @'
root = true

[*]
charset = utf-8
end_of_line = crlf
insert_final_newline = true
indent_style = space
indent_size = 4

[*.{json,yml,yaml,md}]
indent_size = 2
'@
}

function Get-VsCodeSettingsContent {
    return @'
{
  "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
  "cmake.buildDirectory": "${workspaceFolder}/build",
  "files.associations": {
    "*.h": "c"
  }
}
'@
}

function Get-VsCodeTasksContent {
    param([string]$BuildTarget = 'app')
    $content = @'
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "cdo: build",
      "type": "shell",
      "command": "powershell",
      "args": [
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        ".cdo/bin/cdo.ps1",
        "build",
        "__CDO_BUILD_TARGET__"
      ],
      "group": "build",
      "problemMatcher": "$gcc"
    },
    {
      "label": "cdo: test",
      "type": "shell",
      "command": "powershell",
      "args": [
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        ".cdo/bin/cdo.ps1",
        "test"
      ],
      "group": "test",
      "problemMatcher": "$gcc"
    }
  ]
}
'@
    return ($content -replace '__CDO_BUILD_TARGET__', $BuildTarget)
}

function Get-VsCodeLaunchContent {
    param([string]$ExecutableTarget = 'app')
    $content = @'
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug __CDO_EXECUTABLE_TARGET__",
      "type": "cppvsdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/Debug/__CDO_EXECUTABLE_TARGET__.exe",
      "args": [],
      "cwd": "${workspaceFolder}",
      "preLaunchTask": "cdo: build"
    }
  ]
}
'@
    return ($content -replace '__CDO_EXECUTABLE_TARGET__', $ExecutableTarget)
}

function Get-VsCodeExtensionsContent {
    return @'
{
  "recommendations": [
    "ms-vscode.cpptools",
    "ms-vscode.cmake-tools"
  ]
}
'@
}

function Get-GitHubCiContent {
    param([string]$BuildTarget = 'app')
    $content = @'
name: ci

on:
  push:
  pull_request:

jobs:
  windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Doctor
        shell: pwsh
        run: ./.cdo/bin/cdo.ps1 doctor
      - name: Build
        shell: pwsh
        run: ./.cdo/bin/cdo.ps1 build __CDO_BUILD_TARGET__ --release
      - name: Test
        shell: pwsh
        run: ./.cdo/bin/cdo.ps1 test --release
'@
    return ($content -replace '__CDO_BUILD_TARGET__', $BuildTarget)
}

function Get-ProjectReadmeContent {
    param([string]$ProjectName, [string]$Skeleton = 'cli')
    $skeletonName = Resolve-CdoSkeletonName -Name $Skeleton
    $buildTarget = if ($skeletonName -eq 'shared-lib') { 'lib' } else { 'app' }
    $runLine = if ($skeletonName -eq 'shared-lib') { '' } else { "cdo run app`n" }
    $dependencyText = if ($skeletonName -eq 'sdl3') {
        @'

This project uses the `sdl3` skeleton. Add SDL3 before building:

```powershell
cdo add sdl3
```
'@
    } else {
        ''
    }
    $content = @'
# __CDO_PROJECT_NAME__

Portable C project generated by `cdo` using the `__CDO_SKELETON__` skeleton.

## First Run

```powershell
. .\.cdo\activate.ps1
cdo doctor
cdo build __CDO_BUILD_TARGET__
__CDO_RUN_LINE__cdo test
```
__CDO_DEPENDENCY_TEXT__

## Dependencies

Add catalog dependencies with:

```powershell
cdo add sdl3
```

`cdo` keeps dependency metadata in `project.cdo.json` and regenerates `cmake/cdo_deps.cmake` for CMake.
'@
    return ($content `
        -replace '__CDO_PROJECT_NAME__', $ProjectName `
        -replace '__CDO_SKELETON__', $skeletonName `
        -replace '__CDO_BUILD_TARGET__', $buildTarget `
        -replace '__CDO_RUN_LINE__', $runLine `
        -replace '__CDO_DEPENDENCY_TEXT__', $dependencyText)
}

function Get-CdoCmdContent {
    return @'
@echo off
set "SCRIPT_DIR=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%cdo.ps1" %*
'@
}

function Get-ActivateContent {
    return @'
$CdoProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$CdoBin = Join-Path $PSScriptRoot 'bin'

if (-not $env:CDO_OLD_PATH) {
    $env:CDO_OLD_PATH = $env:PATH
}

function Add-CdoPathEntry {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return }
    if ((Test-Path -LiteralPath $Path) -and (($env:PATH -split ';') -notcontains $Path)) {
        $env:PATH = "$Path;$env:PATH"
    }
}

Add-CdoPathEntry -Path $CdoBin

$CdoTools = Join-Path $CdoProjectRoot '.cdo\tools'
if (Test-Path -LiteralPath $CdoTools) {
    foreach ($manifestFile in @(Get-ChildItem -LiteralPath $CdoTools -Recurse -Filter 'cdo-tool.json' -ErrorAction SilentlyContinue)) {
        try {
            $manifest = Get-Content -LiteralPath $manifestFile.FullName -Raw | ConvertFrom-Json
            $toolRoot = Split-Path -Parent $manifestFile.FullName
            foreach ($binDir in @($manifest.binDirs)) {
                Add-CdoPathEntry -Path (Join-Path $toolRoot ([string]$binDir))
            }
        } catch {
            Write-Warning "Could not read cdo tool manifest: $($manifestFile.FullName)"
        }
    }
}

$env:CDO_PROJECT_ROOT = $CdoProjectRoot

if (-not $global:CDO_OLD_PROMPT) {
    $oldPrompt = Get-Command prompt -CommandType Function -ErrorAction SilentlyContinue
    if ($oldPrompt) {
        $global:CDO_OLD_PROMPT = $oldPrompt.ScriptBlock
    }
}

function global:prompt {
    $name = Split-Path $env:CDO_PROJECT_ROOT -Leaf
    if ($global:CDO_OLD_PROMPT) {
        "($name) " + (& $global:CDO_OLD_PROMPT)
    } else {
        "($name) PS $($executionContext.SessionState.Path.CurrentLocation)$('>' * ($nestedPromptLevel + 1)) "
    }
}

function global:deactivate_cdo {
    if ($env:CDO_OLD_PATH) {
        $env:PATH = $env:CDO_OLD_PATH
        Remove-Item Env:\CDO_OLD_PATH -ErrorAction SilentlyContinue
    }
    Remove-Item Env:\CDO_PROJECT_ROOT -ErrorAction SilentlyContinue
    if ($global:CDO_OLD_PROMPT) {
        Set-Item function:\prompt $global:CDO_OLD_PROMPT
        Remove-Variable -Name CDO_OLD_PROMPT -Scope Global -ErrorAction SilentlyContinue
    }
    Remove-Item function:\deactivate_cdo -ErrorAction SilentlyContinue
    Remove-Item function:\deactivate -ErrorAction SilentlyContinue
    Write-Host "cdo deactivated"
}

function global:deactivate {
    deactivate_cdo
}

Write-Host "Activated cdo for $CdoProjectRoot"
Write-Host "Use 'deactivate' to restore the previous shell."
'@
}

function Write-ProjectSkeleton {
    param(
        [string]$Root,
        [string]$ProjectName,
        [string]$ProjectId,
        [string]$Skeleton = 'cli',
        [switch]$Force
    )

    $definition = Get-CdoSkeletonDefinition -Skeleton $Skeleton -ProjectName $ProjectName -ProjectId $ProjectId
    $commonRecipe = New-CdoSkeletonRecipe -Directories @('cmake', 'third_party', '.vscode', '.github\workflows') -Files @(
        @{ Path = '.gitignore'; Content = Get-GitignoreContent },
        @{ Path = '.editorconfig'; Content = Get-EditorConfigContent },
        @{ Path = '.vscode\settings.json'; Content = Get-VsCodeSettingsContent },
        @{ Path = '.vscode\tasks.json'; Content = Get-VsCodeTasksContent -BuildTarget $definition.DefaultTarget },
        @{ Path = '.vscode\extensions.json'; Content = Get-VsCodeExtensionsContent },
        @{ Path = '.github\workflows\ci.yml'; Content = Get-GitHubCiContent -BuildTarget $definition.DefaultTarget },
        @{ Path = 'README.md'; Content = Get-ProjectReadmeContent -ProjectName $ProjectName -Skeleton $definition.Name }
    )

    if (-not [string]::IsNullOrWhiteSpace([string]$definition.RunTarget)) {
        $commonRecipe += New-CdoSkeletonRecipe -Files @(
            @{ Path = '.vscode\launch.json'; Content = Get-VsCodeLaunchContent -ExecutableTarget $definition.RunTarget }
        )
    }

    $recipe = @($definition.Recipe) + @($commonRecipe)
    Invoke-CdoSkeletonRecipe -Root $Root -Recipe $recipe -Force:$Force | Out-Null
}

function Install-CdoSelf {
    param([string]$Root, [string]$SourceDir = $ScriptDir)

    $cdoDir = Join-Path $Root '.cdo'
    $binDir = Join-Path $cdoDir 'bin'
    New-CdoDirectory -Path $binDir
    New-CdoDirectory -Path (Join-Path $cdoDir 'cache')

    foreach ($file in @('cdo.ps1', 'CdoHelpers.ps1', 'CdoSkeletons.ps1', 'CdoTools.ps1', 'CdoShaders.ps1', 'CdoSources.ps1', 'catalog.json')) {
        $source = Join-Path $SourceDir $file
        $destination = Join-Path $binDir $file
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Cannot vendor CDo: missing $source"
        }
        if ([System.IO.Path]::GetFullPath($source) -ne [System.IO.Path]::GetFullPath($destination)) {
            Copy-Item -LiteralPath $source -Destination $destination -Force
        }
    }

    Write-CdoTextFile -Path (Join-Path $binDir 'cdo.cmd') -Content (Get-CdoCmdContent)
    Write-CdoTextFile -Path (Join-Path $cdoDir 'activate.ps1') -Content (Get-ActivateContent)
    Write-CdoLog -Level OK -Message "vendored cdo into $cdoDir"
}

function Initialize-ProjectMinimal {
    param(
        [string]$Path,
        [string]$Name,
        [switch]$DryRun,
        [switch]$Force,
        [switch]$NoSelf
    )

    $root = Resolve-CdoPath -Path $Path
    $projectName = $Name
    if ([string]::IsNullOrWhiteSpace($projectName)) {
        $projectName = Split-Path $root -Leaf
        if ([string]::IsNullOrWhiteSpace($projectName)) { $projectName = 'c_project' }
    }
    $projectId = Get-CdoSafeName -Name $projectName

    if ($DryRun) {
        Write-CdoLog -Level INFO -Message "init preview for $root (minimal, no skeleton)"
        Write-Host "Would create project.cdo.json with empty targets array"
        Write-Host "Would create README.md, .gitignore"
        Write-Host "Would NOT create CMakeLists.txt, src/, include/, or tests/"
        if (-not $NoSelf) { Write-Host "Would vendor cdo into .cdo/ and create .cdo/activate.ps1" }
        return
    }

    New-CdoDirectory -Path $root
    Write-CdoLog -Level INFO -Message "initializing $projectName at $root (no skeleton)"

    # Create project.cdo.json with schemaVersion 2 and empty targets
    $manifestPath = Join-Path $root 'project.cdo.json'
    if ((Test-Path -LiteralPath $manifestPath) -and -not $Force) {
        Write-CdoLog -Level INFO -Message "kept existing $manifestPath"
    } else {
        $config = [ordered]@{
            schema        = 'https://cdo.dev/schemas/project.v1.json'
            schemaVersion = 2
            name          = $projectName
            id            = $projectId
            version       = '0.1.0'
            cStandard     = 17
            cppStandard   = 20
            targets       = @()
            build         = [ordered]@{
                directory     = 'build'
                defaultConfig = 'Debug'
                generator     = 'Unix Makefiles'
            }
            dependencies  = [ordered]@{}
        }
        Save-Config -Config $config -Root $root
        Write-CdoLog -Level OK -Message "wrote $manifestPath"
    }

    # Create README.md
    $readmeContent = @"
# $projectName

Portable C/C++ project managed by CDo.

## First Run

``````powershell
. .\.cdo\activate.ps1
cdo doctor
cdo target create app --type executable
cdo build app
cdo run app
cdo test
``````

## Adding Targets

``````powershell
cdo target create mylib --type library --static
cdo target create game --type executable
cdo target list
``````

## Dependencies

Add catalog dependencies with:

``````powershell
cdo add sdl3
``````
"@
    Write-CdoFileIfMissing -Path (Join-Path $root 'README.md') -Content $readmeContent

    # Create .gitignore
    Write-CdoFileIfMissing -Path (Join-Path $root '.gitignore') -Content (Get-GitignoreContent)

    # Vendor CDo into .cdo/
    if (-not $NoSelf) { Install-CdoSelf -Root $root }

    # Do NOT create CMakeLists.txt, src/, include/, tests/, or any skeleton files

    Write-CdoLog -Level OK -Message "init complete"
    Write-Host ""
    Write-Host "Next:"
    Write-Host "  . .\.cdo\activate.ps1"
    Write-Host "  cdo doctor"
    Write-Host "  cdo target create <name> --type executable"
    Write-Host "  cdo build <name>"
    Write-Host "  cdo test"
}

function Initialize-Project {
    param(
        [string]$Path,
        [string]$Name,
        [string]$Skeleton = 'cli',
        [switch]$DryRun,
        [switch]$Force,
        [switch]$NoSelf,
        [switch]$InstallTools
    )

    $root = Resolve-CdoPath -Path $Path
    $projectName = $Name
    if ([string]::IsNullOrWhiteSpace($projectName)) {
        $projectName = Split-Path $root -Leaf
        if ([string]::IsNullOrWhiteSpace($projectName)) { $projectName = 'c_project' }
    }
    $projectId = Get-CdoSafeName -Name $projectName
    $skeletonName = Resolve-CdoSkeletonName -Name $Skeleton
    $definition = Get-CdoSkeletonDefinition -Skeleton $skeletonName -ProjectName $projectName -ProjectId $projectId

    if ($DryRun) {
        Write-CdoLog -Level INFO -Message "init preview for $root"
        Write-Host "Skeleton: $($definition.Name) - $($definition.Description)"
        Write-Host "Would create src/, include/$projectId/, tests/, cmake/, third_party/, .vscode/, .github/workflows/"
        Write-Host "Would write CMakeLists.txt, project.cdo.json, README.md, .gitignore, VS Code files, CI, and tests"
        $requiredDependencies = @($definition.RequiredDependencies)
        if ($requiredDependencies.Count -gt 0) {
            Write-Host "Required dependency step after init: cdo add $($requiredDependencies -join ', cdo add ')"
        }
        $requiredTools = @($definition.RequiredTools)
        if ($requiredTools.Count -gt 0) {
            Write-Host "Required tool step after init: cdo tool install $($requiredTools -join ', cdo tool install ')"
            if ($InstallTools) { Write-Host "Would install required portable tools because --install-tools was passed" }
        }
        if (-not $NoSelf) { Write-Host "Would vendor cdo into .cdo/ and create .cdo/activate.ps1" }
        return
    }

    New-CdoDirectory -Path $root
    Write-CdoLog -Level INFO -Message "initializing $projectName at $root using $skeletonName skeleton"

    $manifestPath = Join-Path $root 'project.cdo.json'
    if ((Test-Path -LiteralPath $manifestPath) -and -not $Force) {
        Write-CdoLog -Level INFO -Message "kept existing $manifestPath"
        $config = Load-Config -Root $root
    } else {
        $config = New-CdoManifest -ProjectName $projectName -ProjectId $projectId -Skeleton $skeletonName
        Save-Config -Config $config -Root $root
        Write-CdoLog -Level OK -Message "wrote $manifestPath"
    }

    Write-ProjectSkeleton -Root $root -ProjectName $projectName -ProjectId $projectId -Skeleton $skeletonName -Force:$Force
    if (-not $NoSelf) { Install-CdoSelf -Root $root }
    Write-CmakeDependencies -Root $root -Config $config
    if ($InstallTools) {
        Install-CdoRequiredTools -Root $root -Config $config -Force:$Force
    }

    Write-CdoLog -Level OK -Message "init complete"
    Write-Host ""
    Write-Host "Next:"
    Write-Host "  . .\.cdo\activate.ps1"
    Write-Host "  cdo doctor"
    foreach ($dep in @($definition.RequiredDependencies)) {
        Write-Host "  cdo add $dep"
    }
    foreach ($tool in @($definition.RequiredTools)) {
        if ($InstallTools) {
            Write-Host "  cdo tool doctor"
        } else {
            Write-Host "  cdo tool install $tool"
        }
    }
    Write-Host "  cdo build $($definition.DefaultTarget)"
    if (-not [string]::IsNullOrWhiteSpace([string]$definition.RunTarget)) {
        Write-Host "  cdo run $($definition.RunTarget)"
    }
    Write-Host "  cdo test"
}

function Setup-W64DevKit {
    param([string]$WorkDir)

    $toolsDir = Join-Path $WorkDir '.cdo\tools'
    $w64dir = Join-Path $toolsDir 'w64devkit'
    if (Test-Path -LiteralPath $w64dir) {
        return $w64dir
    }

    $catalogInfo = Get-CatalogEntry -Name 'w64devkit'
    $entry = $catalogInfo.Entry
    $url = [string](Get-CdoProperty -Object $entry -Name 'url')
    if ([string]::IsNullOrWhiteSpace($url)) {
        throw "w64devkit catalog entry has no URL."
    }

    New-CdoDirectory -Path $toolsDir
    $zipPath = Join-Path $toolsDir 'w64devkit.zip'
    Write-CdoLog -Level INFO -Message "downloading w64devkit"
    Get-CdoCachedDownload -Url $url -Destination $zipPath -Refresh:$false | Out-Null
    Extract-CdoArchive -ArchivePath $zipPath -OutDir $toolsDir
    Remove-Item -LiteralPath $zipPath -Force

    $subdirs = @(Get-ChildItem -LiteralPath $toolsDir -Directory | Where-Object { $_.Name -ne 'cache' })
    if (-not (Test-Path -LiteralPath $w64dir) -and $subdirs.Count -eq 1) {
        Rename-Item -LiteralPath $subdirs[0].FullName -NewName 'w64devkit'
    }

    if (-not (Test-Path -LiteralPath $w64dir)) {
        $gcc = Get-ChildItem -LiteralPath $toolsDir -Recurse -Filter 'gcc.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($gcc) {
            $bin = Split-Path -Parent $gcc.FullName
            $kit = Split-Path -Parent $bin
            Move-Item -LiteralPath $kit -Destination $w64dir -Force
        }
    }

    if (-not (Test-Path -LiteralPath $w64dir)) {
        throw "Downloaded w64devkit, but could not identify its root under $toolsDir."
    }

    return $w64dir
}

function Ensure-Requirements {
    param(
        [string]$Root,
        [switch]$Fix
    )

    $ok = $true
    foreach ($tool in @('git', 'cmake')) {
        if (Test-CdoCommand -Name $tool) {
            Write-CdoLog -Level OK -Message "$tool found"
        } else {
            Write-CdoLog -Level WARN -Message "$tool missing"
            $ok = $false
        }
    }

    $compiler = @('cl', 'gcc', 'clang') | Where-Object { Test-CdoCommand -Name $_ } | Select-Object -First 1
    if ($compiler) {
        Write-CdoLog -Level OK -Message "C compiler found: $compiler"
    } else {
        Write-CdoLog -Level WARN -Message "no C compiler found in PATH (checked cl, gcc, clang)"
        if ($Fix -and (Test-CdoWindows)) {
            $kit = Setup-W64DevKit -WorkDir $Root
            $bin = Join-Path $kit 'bin'
            $env:PATH = "$bin;$env:PATH"
            if (Test-CdoCommand -Name 'gcc') {
                Write-CdoLog -Level OK -Message "gcc found from $bin"
            } else {
                $ok = $false
            }
        } else {
            $ok = $false
        }
    }

    if (-not (Test-CdoCommand -Name 'cmake')) {
        Write-Host "Install CMake from https://cmake.org/download/ or put it in PATH." -ForegroundColor Yellow
    }

    return $ok
}

function Get-RelativeProjectPath {
    param([string]$Root, [string]$Path)
    return ConvertTo-CdoForwardSlashPath -Path (Get-CdoRelativePath -From $Root -To $Path)
}

function Select-PreferredFile {
    param([object[]]$Files)
    if ($Files.Count -eq 0) { return $null }
    $preferred = $Files | Where-Object { $_.FullName -match 'x86_64|amd64|x64' } | Select-Object -First 1
    if ($preferred) { return $preferred }
    return $Files[0]
}

function Resolve-DependencyLayout {
    param(
        [string]$Root,
        [string]$Name,
        [object]$Entry,
        [string]$DependencyRoot
    )

    $package = Get-CdoProperty -Object $Entry -Name 'cmakePackage'
    if ($package) {
        $configName = "${package}Config.cmake"
        $configs = @(Get-ChildItem -LiteralPath $DependencyRoot -Recurse -Filter $configName -ErrorAction SilentlyContinue)
        $configFile = Select-PreferredFile -Files $configs
        if ($null -eq $configFile) {
            Write-CdoLog -Level WARN -Message "$Name was added, but no $configName was found under $DependencyRoot"
            return [ordered]@{
                kind = 'cmake-package'
                package = $package
                target = [string](Get-CdoProperty -Object $Entry -Name 'cmakeTarget' -Default "$package::$package")
                prefix = Get-RelativeProjectPath -Root $Root -Path $DependencyRoot
                runtimeDll = $null
            }
        }

        $configDir = Split-Path -Parent $configFile.FullName
        $prefix = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $configDir))

        $runtimeNames = @(Get-CdoProperty -Object $Entry -Name 'runtimeDlls' -Default @("${package}.dll"))
        $runtimeDll = $null
        foreach ($runtimeName in $runtimeNames) {
            $dlls = @(Get-ChildItem -LiteralPath $DependencyRoot -Recurse -Filter $runtimeName -ErrorAction SilentlyContinue)
            $selected = Select-PreferredFile -Files $dlls
            if ($selected) {
                $runtimeDll = Get-RelativeProjectPath -Root $Root -Path $selected.FullName
                break
            }
        }

        return [ordered]@{
            kind = 'cmake-package'
            package = $package
            target = [string](Get-CdoProperty -Object $Entry -Name 'cmakeTarget' -Default "$package::$package")
            sharedTarget = [string](Get-CdoProperty -Object $Entry -Name 'cmakeSharedTarget' -Default '')
            prefix = Get-RelativeProjectPath -Root $Root -Path $prefix
            runtimeDll = $runtimeDll
        }
    }

    $includeDir = Get-ChildItem -LiteralPath $DependencyRoot -Recurse -Directory -Filter 'include' -ErrorAction SilentlyContinue | Select-Object -First 1
    $includeRel = $null
    if ($includeDir) { $includeRel = Get-RelativeProjectPath -Root $Root -Path $includeDir.FullName }

    return [ordered]@{
        kind = 'generic'
        includeDir = $includeRel
    }
}

function Write-CmakeDependencies {
    param(
        [string]$Root,
        [object]$Config
    )

    New-CdoDirectory -Path (Join-Path $Root 'cmake')
    $deps = Get-DependencyMap -Config $Config
    $lines = New-Object 'System.Collections.Generic.List[string]'
    $applyLines = New-Object 'System.Collections.Generic.List[string]'
    $runtimeLines = New-Object 'System.Collections.Generic.List[string]'

    $lines.Add('# Generated by cdo. Regenerate with: cdo sync')
    $lines.Add('get_filename_component(CDO_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)')
    $lines.Add('')

    foreach ($name in $deps.Keys) {
        $dep = $deps[$name]
        $var = Get-CdoVariableName -Name $name
        $cmake = Get-CdoProperty -Object $dep -Name 'cmake'
        $kind = [string](Get-CdoProperty -Object $cmake -Name 'kind' -Default 'generic')

        $lines.Add("# Dependency: $name")
        if ($kind -eq 'cmake-package') {
            $package = [string](Get-CdoProperty -Object $cmake -Name 'package')
            $target = [string](Get-CdoProperty -Object $cmake -Name 'target' -Default "$package::$package")
            $sharedTarget = [string](Get-CdoProperty -Object $cmake -Name 'sharedTarget' -Default '')
            $prefix = [string](Get-CdoProperty -Object $cmake -Name 'prefix' -Default (Get-CdoProperty -Object $dep -Name 'path' -Default "third_party/$name"))
            $runtimeDll = [string](Get-CdoProperty -Object $cmake -Name 'runtimeDll' -Default '')

            $lines.Add('set(CDO_DEP_' + $var + '_PREFIX "${CDO_PROJECT_ROOT}/' + $prefix + '")')
            if (-not [string]::IsNullOrWhiteSpace($runtimeDll)) {
                $lines.Add('set(CDO_DEP_' + $var + '_RUNTIME_DLL "${CDO_PROJECT_ROOT}/' + $runtimeDll + '")')
            }
            $lines.Add('if(NOT TARGET ' + $target + ')')
            $lines.Add('    find_package(' + $package + ' CONFIG REQUIRED PATHS "${CDO_DEP_' + $var + '_PREFIX}" NO_DEFAULT_PATH)')
            $lines.Add('endif()')
            $lines.Add('')

            $applyLines.Add('    target_link_libraries(${target} PUBLIC ' + $target + ')')

            if (-not [string]::IsNullOrWhiteSpace($sharedTarget) -or -not [string]::IsNullOrWhiteSpace($runtimeDll)) {
                $runtimeLines.Add('    if(WIN32)')
                if (-not [string]::IsNullOrWhiteSpace($sharedTarget)) {
                    $runtimeLines.Add('        if(TARGET ' + $sharedTarget + ')')
                    $runtimeLines.Add('            add_custom_command(TARGET ${target} POST_BUILD')
                    $runtimeLines.Add('                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:' + $sharedTarget + '>" "$<TARGET_FILE_DIR:${target}>"')
                    $runtimeLines.Add('                VERBATIM)')
                    if (-not [string]::IsNullOrWhiteSpace($runtimeDll)) {
                        $runtimeLines.Add('        elseif(EXISTS "${CDO_DEP_' + $var + '_RUNTIME_DLL}")')
                        $runtimeLines.Add('            add_custom_command(TARGET ${target} POST_BUILD')
                        $runtimeLines.Add('                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CDO_DEP_' + $var + '_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${target}>"')
                        $runtimeLines.Add('                VERBATIM)')
                    }
                    $runtimeLines.Add('        endif()')
                } elseif (-not [string]::IsNullOrWhiteSpace($runtimeDll)) {
                    $runtimeLines.Add('        if(EXISTS "${CDO_DEP_' + $var + '_RUNTIME_DLL}")')
                    $runtimeLines.Add('            add_custom_command(TARGET ${target} POST_BUILD')
                    $runtimeLines.Add('                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CDO_DEP_' + $var + '_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${target}>"')
                    $runtimeLines.Add('                VERBATIM)')
                    $runtimeLines.Add('        endif()')
                }
                $runtimeLines.Add('    endif()')
            }
        } else {
            $includeDir = [string](Get-CdoProperty -Object $cmake -Name 'includeDir' -Default '')
            if (-not [string]::IsNullOrWhiteSpace($includeDir)) {
                $lines.Add('set(CDO_DEP_' + $var + '_INCLUDE "${CDO_PROJECT_ROOT}/' + $includeDir + '")')
                $applyLines.Add('    if(EXISTS "${CDO_DEP_' + $var + '_INCLUDE}")')
                $applyLines.Add('        target_include_directories(${target} PUBLIC "${CDO_DEP_' + $var + '_INCLUDE}")')
                $applyLines.Add('    endif()')
            } else {
                $lines.Add('message(WARNING "cdo dependency ' + $name + ' has no automatic CMake integration yet")')
            }
            $lines.Add('')
        }
    }

    $lines.Add('function(cdo_apply_dependencies target)')
    $lines.Add('    if(NOT TARGET ${target})')
    $lines.Add('        message(FATAL_ERROR "cdo_apply_dependencies called for unknown target: ${target}")')
    $lines.Add('    endif()')
    foreach ($line in $applyLines) { $lines.Add($line) }
    $lines.Add('endfunction()')
    $lines.Add('')
    $lines.Add('function(cdo_copy_runtime_dependencies target)')
    $lines.Add('    if(NOT TARGET ${target})')
    $lines.Add('        message(FATAL_ERROR "cdo_copy_runtime_dependencies called for unknown target: ${target}")')
    $lines.Add('    endif()')
    foreach ($line in $runtimeLines) { $lines.Add($line) }
    $lines.Add('endfunction()')
    $lines.Add('')

    Write-CdoTextFile -Path (Join-Path $Root 'cmake\cdo_deps.cmake') -Content (($lines -join [Environment]::NewLine) + [Environment]::NewLine)
    Write-CdoLog -Level OK -Message "synced cmake/cdo_deps.cmake"
}

function Sync-CdoProject {
    param(
        [string]$Root,
        [object]$Config,
        [switch]$RefreshLayouts,
        [switch]$Self
    )

    $deps = Get-DependencyMap -Config $Config
    $changed = $false
    foreach ($name in @($deps.Keys)) {
        $dep = $deps[$name]
        $path = [string](Get-CdoProperty -Object $dep -Name 'path' -Default "third_party/$name")
        $abs = Join-Path $Root $path
        if (-not (Test-Path -LiteralPath $abs)) {
            Write-CdoLog -Level WARN -Message "$name is active but $path does not exist"
            continue
        }
        $cmake = Get-CdoProperty -Object $dep -Name 'cmake'
        if ($RefreshLayouts -or $null -eq $cmake) {
            try {
                $catalogInfo = Get-CatalogEntry -Name $name
                $layout = Resolve-DependencyLayout -Root $Root -Name $name -Entry $catalogInfo.Entry -DependencyRoot $abs
                Set-CdoProperty -Object $dep -Name 'cmake' -Value $layout
                $deps[$name] = $dep
                $changed = $true
            } catch {
                Write-CdoLog -Level WARN -Message "could not refresh $name metadata: $($_.Exception.Message)"
            }
        }
    }

    if ($changed) {
        Set-DependencyMap -Config $Config -Dependencies $deps
        Save-Config -Config $Config -Root $Root
    }

    if ($Self) { Install-CdoSelf -Root $Root }
    Write-CmakeDependencies -Root $Root -Config $Config
}

function Add-Dependency {
    param(
        [string]$Name,
        [string]$Root,
        [string]$ArchivePath,
        [switch]$Force,
        [switch]$RefreshCache
    )

    $catalogInfo = Get-CatalogEntry -Name $Name
    $key = $catalogInfo.Key
    $entry = $catalogInfo.Entry
    $config = Load-Config -Root $Root
    $deps = Get-DependencyMap -Config $config

    if ($deps.Contains($key) -and -not $Force) {
        Write-CdoLog -Level INFO -Message "$key is already active. Use --force to reinstall."
        return
    }

    $depRoot = Join-Path $Root "third_party\$key"
    if ((Test-Path -LiteralPath $depRoot) -and $Force) {
        Remove-Item -LiteralPath $depRoot -Recurse -Force
    }
    New-CdoDirectory -Path $depRoot

    $archiveToExtract = $ArchivePath
    if ([string]::IsNullOrWhiteSpace($archiveToExtract)) {
        $url = [string](Get-CdoProperty -Object $entry -Name 'url')
        if ([string]::IsNullOrWhiteSpace($url)) {
            throw "$key has no URL in catalog.json. Use --archive <path>."
        }
        $uri = [System.Uri]$url
        $leaf = [System.IO.Path]::GetFileName($uri.LocalPath)
        if ([string]::IsNullOrWhiteSpace($leaf)) { $leaf = "$key.archive" }
        $archiveToExtract = Join-Path (Join-Path $Root '.cdo\cache') $leaf
        try {
            Get-CdoCachedDownload -Url $url -Destination $archiveToExtract -FileName $leaf -Refresh:$RefreshCache | Out-Null
        } catch {
            Write-CdoLog -Level ERROR -Message "Could not download $key. URL: $url"
            throw
        }
    } else {
        $archiveToExtract = Resolve-CdoPath -Path $archiveToExtract
    }

    Write-CdoLog -Level INFO -Message "extracting $key into third_party/$key"
    Extract-CdoArchive -ArchivePath $archiveToExtract -OutDir $depRoot

    $layout = Resolve-DependencyLayout -Root $Root -Name $key -Entry $entry -DependencyRoot $depRoot
    $deps[$key] = [ordered]@{
        name = $key
        displayName = [string](Get-CdoProperty -Object $entry -Name 'name' -Default $key)
        version = [string](Get-CdoProperty -Object $entry -Name 'version' -Default '')
        path = "third_party/$key"
        sourceUrl = [string](Get-CdoProperty -Object $entry -Name 'url' -Default '')
        addedAt = Get-CdoTimestamp
        cmake = $layout
    }

    Set-DependencyMap -Config $config -Dependencies $deps
    Save-Config -Config $config -Root $Root
    Write-CmakeDependencies -Root $Root -Config $config
    Write-CdoLog -Level OK -Message "$key added"
}

function Remove-Dependency {
    param(
        [string]$Name,
        [string]$Root,
        [switch]$DeleteFiles
    )

    $config = Load-Config -Root $Root
    $deps = Get-DependencyMap -Config $config
    $key = $null
    foreach ($depName in $deps.Keys) {
        if ($depName -ieq $Name) { $key = $depName; break }
    }
    if (-not $key) {
        Write-CdoLog -Level WARN -Message "$Name is not active"
        return
    }

    $path = [string](Get-CdoProperty -Object $deps[$key] -Name 'path' -Default "third_party/$key")
    $deps.Remove($key)
    Set-DependencyMap -Config $config -Dependencies $deps
    Save-Config -Config $config -Root $Root
    Write-CmakeDependencies -Root $Root -Config $config

    if ($DeleteFiles) {
        $abs = Join-Path $Root $path
        if (Test-Path -LiteralPath $abs) {
            Remove-Item -LiteralPath $abs -Recurse -Force
            Write-CdoLog -Level OK -Message "deleted $path"
        }
    }

    Write-CdoLog -Level OK -Message "$key removed"
}

function List-Dependencies {
    param([string]$Root)

    Write-Host "Catalog:"
    foreach ($name in (Get-CatalogNames)) {
        $info = Get-CatalogEntry -Name $name
        $entry = $info.Entry
        $display = [string](Get-CdoProperty -Object $entry -Name 'name' -Default $name)
        $version = [string](Get-CdoProperty -Object $entry -Name 'version' -Default '')
        Write-Host ("  {0,-14} {1,-20} {2}" -f $name, $display, $version)
    }

    if ($Root -and (Test-Path -LiteralPath (Join-Path $Root 'project.cdo.json'))) {
        $config = Load-Config -Root $Root
        $deps = Get-DependencyMap -Config $config
        Write-Host ""
        Write-Host "Active:"
        if ($deps.Count -eq 0) {
            Write-Host "  (none)"
        } else {
            foreach ($name in $deps.Keys) {
                $dep = $deps[$name]
                $version = [string](Get-CdoProperty -Object $dep -Name 'version' -Default '')
                $path = [string](Get-CdoProperty -Object $dep -Name 'path' -Default '')
                Write-Host ("  {0,-14} {1,-12} {2}" -f $name, $version, $path)
            }
        }
    }
}

function Invoke-CdoBuildShaders {
    param(
        [string]$Root,
        [object]$Config,
        [object]$Parsed
    )

    if (Get-ParsedFlag -Parsed $Parsed -Name 'no-shaders') { return }
    if (-not (Get-Command -Name 'Test-CdoShaderProject' -CommandType Function -ErrorAction SilentlyContinue)) { return }
    if (-not (Test-CdoShaderProject -Config $Config)) { return }

    Write-CdoLog -Level INFO -Message "compiling shaders"
    Invoke-CdoShaderCompile -Root $Root -Strict
}

function Configure-Project {
    param(
        [string]$Root,
        [object]$Config,
        [string]$BuildConfig,
        [object]$Parsed
    )

    Add-CdoToolPathForProcess -Root $Root

    if (-not (Test-CdoCommand -Name 'cmake')) {
        throw "cmake is not available in PATH. Run 'cdo doctor'."
    }

    Sync-CdoProject -Root $Root -Config $Config
    $buildDir = Get-BuildDirectory -Config $Config -Root $Root
    New-CdoDirectory -Path $buildDir

    $args = @('-S', $Root, '-B', $buildDir, "-DCMAKE_BUILD_TYPE=$BuildConfig")
    $generator = Get-ParsedOption -Parsed $Parsed -Name 'generator'
    if (-not $generator) {
        $build = Get-CdoProperty -Object $Config -Name 'build'
        $generator = Get-CdoProperty -Object $build -Name 'generator' -Default ''
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$generator)) {
        $args += @('-G', [string]$generator)
    }

    # Use toolchain file if present
    $toolchainFile = Join-Path $Root 'cmake\w64devkit-toolchain.cmake'
    if (Test-Path -LiteralPath $toolchainFile) {
        $args += @("-DCMAKE_TOOLCHAIN_FILE=$toolchainFile")
    }

    Write-CdoLog -Level INFO -Message "configuring CMake ($BuildConfig)"
    $null = Invoke-CdoNative -FilePath 'cmake' -Arguments $args -WorkingDirectory $Root -ThrowOnError
}

function Build-App {
    param(
        [string]$Root,
        [object]$Parsed,
        [string]$AppName
    )

    $config = Load-Config -Root $Root
    $buildConfig = Get-BuildConfig -Parsed $Parsed -Config $config
    $targets = Resolve-CdoTargets -Config $config

    if ([string]::IsNullOrWhiteSpace($AppName)) {
        $resolved = Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable', 'library', 'shared-library') -Command 'build'
    } else {
        $resolved = Get-CdoTargetByName -Targets $targets -Name $AppName
    }

    $target = Get-CdoProperty -Object $resolved -Name 'target' -Default (Get-CdoProperty -Object $resolved -Name 'name')

    Invoke-CdoBuildShaders -Root $Root -Config $config -Parsed $Parsed
    Configure-Project -Root $Root -Config $config -BuildConfig $buildConfig -Parsed $Parsed
    $buildDir = Get-BuildDirectory -Config $config -Root $Root

    Write-CdoLog -Level INFO -Message "building $target ($buildConfig)"
    $null = Invoke-CdoNative -FilePath 'cmake' -Arguments @('--build', $buildDir, '--config', $buildConfig, '--target', $target) -WorkingDirectory $Root -ThrowOnError
}

function Get-AppExecutableCandidates {
    param(
        [string]$BuildDir,
        [string]$BuildConfig,
        [string]$Target
    )

    $names = @($Target)
    if (Test-CdoWindows) { $names = @("$Target.exe", $Target) }

    $candidates = @()
    foreach ($name in $names) {
        $candidates += Join-Path (Join-Path $BuildDir $BuildConfig) $name
        $candidates += Join-Path $BuildDir $name
    }
    return $candidates
}

function Get-DependencyRuntimeDirs {
    param([string]$Root, [object]$Config)
    $dirs = New-Object 'System.Collections.Generic.List[string]'
    $deps = Get-DependencyMap -Config $Config
    foreach ($name in $deps.Keys) {
        $cmake = Get-CdoProperty -Object $deps[$name] -Name 'cmake'
        $dll = [string](Get-CdoProperty -Object $cmake -Name 'runtimeDll' -Default '')
        if (-not [string]::IsNullOrWhiteSpace($dll)) {
            $dir = Split-Path -Parent (Join-Path $Root $dll)
            if ((Test-Path -LiteralPath $dir) -and -not $dirs.Contains($dir)) {
                $dirs.Add($dir)
            }
        }
    }
    return $dirs.ToArray()
}

function Run-App {
    param(
        [string]$Root,
        [object]$Parsed,
        [string]$AppName,
        [string[]]$ExtraArgs = @()
    )

    $config = Load-Config -Root $Root
    $buildConfig = Get-BuildConfig -Parsed $Parsed -Config $config
    $targets = Resolve-CdoTargets -Config $config

    if ([string]::IsNullOrWhiteSpace($AppName)) {
        $resolved = Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable') -Command 'run'
    } else {
        $resolved = Get-CdoTargetByName -Targets $targets -Name $AppName
        $type = Get-CdoProperty -Object $resolved -Name 'type'
        if ($type -ne 'executable') {
            throw "Cannot run target '$AppName': only executable targets can be run. Target type is '$type'."
        }
    }

    $target = Get-CdoProperty -Object $resolved -Name 'target' -Default (Get-CdoProperty -Object $resolved -Name 'name')
    $buildDir = Get-BuildDirectory -Config $config -Root $Root

    $exe = $null
    foreach ($candidate in (Get-AppExecutableCandidates -BuildDir $buildDir -BuildConfig $buildConfig -Target $target)) {
        if (Test-Path -LiteralPath $candidate) { $exe = $candidate; break }
    }

    if (-not $exe) {
        Write-CdoLog -Level INFO -Message "$target is not built yet; building first"
        Build-App -Root $Root -Parsed $Parsed -AppName $AppName
        foreach ($candidate in (Get-AppExecutableCandidates -BuildDir $buildDir -BuildConfig $buildConfig -Target $target)) {
            if (Test-Path -LiteralPath $candidate) { $exe = $candidate; break }
        }
    }

    if (-not $exe) { throw "Could not find executable for $target under $buildDir." }

    $manifestArgs = @(Get-CdoProperty -Object $resolved -Name 'args' -Default @())
    $runArgs = @($manifestArgs + $ExtraArgs)
    Add-CdoToolPathForProcess -Root $Root
    $runtimeDirs = @(Get-DependencyRuntimeDirs -Root $Root -Config $config)
    $oldPath = $env:PATH
    try {
        if ($runtimeDirs.Count -gt 0) {
            $env:PATH = (($runtimeDirs -join ';') + ';' + $env:PATH)
        }
        Write-CdoLog -Level INFO -Message "running $exe"
        & $exe @runArgs
        $exitCode = $LASTEXITCODE
        if ($null -eq $exitCode) { $exitCode = 0 }
        if ($exitCode -ne 0) { throw "$target exited with code $exitCode" }
    } finally {
        $env:PATH = $oldPath
    }
}

function Run-Tests {
    param([string]$Root, [object]$Parsed, [string]$TargetName = '')

    $config = Load-Config -Root $Root
    $buildConfig = Get-BuildConfig -Parsed $Parsed -Config $config
    Configure-Project -Root $Root -Config $config -BuildConfig $buildConfig -Parsed $Parsed
    $buildDir = Get-BuildDirectory -Config $config -Root $Root
    $targets = Resolve-CdoTargets -Config $config

    if ([string]::IsNullOrWhiteSpace($TargetName)) {
        # Build all test targets
        $testTargets = @($targets | Where-Object {
            (Get-CdoProperty -Object $_ -Name 'type') -eq 'test'
        })
        if ($testTargets.Count -eq 0) {
            throw "No test targets declared in project.cdo.json."
        }
        foreach ($t in $testTargets) {
            $cmakeTarget = Get-CdoProperty -Object $t -Name 'target' -Default (Get-CdoProperty -Object $t -Name 'name')
            Write-CdoLog -Level INFO -Message "building test target: $cmakeTarget ($buildConfig)"
            $null = Invoke-CdoNative -FilePath 'cmake' -Arguments @('--build', $buildDir, '--config', $buildConfig, '--target', $cmakeTarget) -WorkingDirectory $Root -ThrowOnError
        }
    } else {
        # Build specific test target with per-target resolution
        $resolved = $null
        # First try exact match (handles both 'engine_tests' and 'engine' names)
        $directMatch = Get-CdoTargetByName -Targets $targets -Name $TargetName
        if ($null -ne $directMatch) {
            $type = Get-CdoProperty -Object $directMatch -Name 'type'
            if ($type -eq 'test') {
                $resolved = $directMatch
            } else {
                # Try appending _tests for non-test targets
                $testName = "${TargetName}_tests"
                $testMatch = Get-CdoTargetByName -Targets $targets -Name $testName
                if ($null -ne $testMatch -and (Get-CdoProperty -Object $testMatch -Name 'type') -eq 'test') {
                    $resolved = $testMatch
                } else {
                    throw "No test target found for '$TargetName'. Expected '${testName}' to exist."
                }
            }
        } else {
            throw "Target '$TargetName' not found."
        }
        $cmakeTarget = Get-CdoProperty -Object $resolved -Name 'target' -Default (Get-CdoProperty -Object $resolved -Name 'name')
        Write-CdoLog -Level INFO -Message "building test target: $cmakeTarget ($buildConfig)"
        $null = Invoke-CdoNative -FilePath 'cmake' -Arguments @('--build', $buildDir, '--config', $buildConfig, '--target', $cmakeTarget) -WorkingDirectory $Root -ThrowOnError
    }

    Write-CdoLog -Level INFO -Message "running tests"
    $null = Invoke-CdoNative -FilePath 'ctest' -Arguments @('--test-dir', $buildDir, '-C', $buildConfig, '--output-on-failure') -WorkingDirectory $Root -ThrowOnError
}

function Clean-Project {
    param([string]$Root)
    $config = Load-Config -Root $Root
    $buildDir = Get-BuildDirectory -Config $config -Root $Root
    if (Test-Path -LiteralPath $buildDir) {
        Remove-Item -LiteralPath $buildDir -Recurse -Force
        Write-CdoLog -Level OK -Message "removed $buildDir"
    } else {
        Write-CdoLog -Level INFO -Message "nothing to clean"
    }
}

function Show-ActivationHelp {
    param([string]$Root)
    $activation = Join-Path $Root '.cdo\activate.ps1'
    if (-not (Test-Path -LiteralPath $activation)) {
        Write-CdoLog -Level WARN -Message ".cdo is not installed yet. Run 'cdo sync' or 'cdo init .'."
    }
    Write-Host "PowerShell activation:"
    Write-Host "  . .\.cdo\activate.ps1"
    Write-Host ""
    Write-Host "Activation prepends .cdo/bin to PATH, sets CDO_PROJECT_ROOT, and adds 'deactivate'."
}

function Invoke-Doctor {
    param([string]$Root, [switch]$Fix)

    Write-CdoLog -Level INFO -Message "project: $Root"
    $requirementsOk = Ensure-Requirements -Root $Root -Fix:$Fix

    $config = $null
    if (Test-Path -LiteralPath (Join-Path $Root 'project.cdo.json')) {
        $config = Load-Config -Root $Root
        Write-CdoLog -Level OK -Message "project.cdo.json found"
    } else {
        Write-CdoLog -Level WARN -Message "project.cdo.json missing"
        $requirementsOk = $false
    }

    if (Test-Path -LiteralPath (Join-Path $Root '.cdo\bin\cdo.ps1')) {
        Write-CdoLog -Level OK -Message "portable cdo found in .cdo/bin"
    } else {
        Write-CdoLog -Level WARN -Message "portable cdo missing; run 'cdo sync'"
    }

    if ($config) {
        $deps = Get-DependencyMap -Config $config
        $requiredDeps = @(Get-CdoProperty -Object $config -Name 'requiredDependencies' -Default @())
        foreach ($required in $requiredDeps) {
            $found = $false
            foreach ($name in $deps.Keys) {
                if ($name -ieq $required) { $found = $true; break }
            }
            if (-not $found) {
                if ($Fix) {
                    Write-CdoLog -Level INFO -Message "installing required dependency: $required"
                    try {
                        Add-Dependency -Name ([string]$required) -Root $Root
                        $config = Load-Config -Root $Root
                        $deps = Get-DependencyMap -Config $config
                        Write-CdoLog -Level OK -Message "required dependency installed: $required"
                    } catch {
                        Write-CdoLog -Level WARN -Message "could not install required dependency ${required}: $($_.Exception.Message)"
                        $requirementsOk = $false
                    }
                } else {
                    Write-CdoLog -Level WARN -Message "required dependency missing: $required (run 'cdo add $required')"
                    $requirementsOk = $false
                }
            }
        }

        $requiredTools = @(Get-CdoProperty -Object $config -Name 'requiredTools' -Default @())
        foreach ($requiredTool in $requiredTools) {
            $toolName = Resolve-CdoToolName -Name ([string]$requiredTool)
            $toolEntry = (Get-CdoToolCatalog)[$toolName]
            $commands = @(Get-CdoProperty -Object $toolEntry -Name 'commands' -Default @($toolName))
            $foundTool = $false
            foreach ($command in $commands) {
                if (Find-CdoToolCommand -Root $Root -Command ([string]$command)) {
                    $foundTool = $true
                    break
                }
            }

            if ($foundTool) {
                Write-CdoLog -Level OK -Message "required tool available: $toolName"
            } elseif ($Fix) {
                Write-CdoLog -Level INFO -Message "installing required tool: $toolName"
                Install-CdoTool -Root $Root -Name $toolName
                if (Find-CdoToolCommand -Root $Root -Command ([string]$commands[0])) {
                    Write-CdoLog -Level OK -Message "required tool installed: $toolName"
                } else {
                    $requirementsOk = $false
                }
            } else {
                Write-CdoLog -Level WARN -Message "required tool missing: $toolName (run 'cdo tool install $toolName')"
                $requirementsOk = $false
            }
        }

        foreach ($name in $deps.Keys) {
            $path = [string](Get-CdoProperty -Object $deps[$name] -Name 'path' -Default "third_party/$name")
            if (Test-Path -LiteralPath (Join-Path $Root $path)) {
                Write-CdoLog -Level OK -Message "dependency active: $name"
            } else {
                if ($Fix) {
                    Write-CdoLog -Level INFO -Message "reinstalling dependency missing on disk: $name"
                    try {
                        Add-Dependency -Name ([string]$name) -Root $Root -Force
                        Write-CdoLog -Level OK -Message "dependency restored: $name"
                    } catch {
                        Write-CdoLog -Level WARN -Message "could not restore dependency ${name}: $($_.Exception.Message)"
                        $requirementsOk = $false
                    }
                } else {
                    Write-CdoLog -Level WARN -Message "dependency missing on disk: $name ($path)"
                    $requirementsOk = $false
                }
            }
        }
    }

    if ($requirementsOk) {
        Write-CdoLog -Level OK -Message "doctor passed"
    } else {
        Write-CdoLog -Level WARN -Message "doctor found issues"
        if (-not $Fix) { Write-Host "Try 'cdo doctor --fix' to install required dependencies and portable tools that cdo can manage." -ForegroundColor Yellow }
    }
}

$parsed = Parse-CdoCliArgs -RawArgs @($args)
if ((Get-ParsedFlag -Parsed $parsed -Name 'debug') -or (Get-ParsedFlag -Parsed $parsed -Name 'verbose')) {
    $Global:CdoDebug = $true
}

if ((Get-ParsedFlag -Parsed $parsed -Name 'help') -or ($parsed.Positionals.Count -eq 0)) {
    Show-Help
    exit 0
}

$command = ([string]$parsed.Positionals[0]).ToLowerInvariant()
$commandArgs = @(Get-CommandPositionals -Parsed $parsed)

try {
    switch ($command) {
        'help' { Show-Help }
        'version' {
            if (Get-ParsedFlag -Parsed $parsed -Name 'all') {
                $root = $null
                try { $root = Resolve-CdoSelfTargetRoot -Parsed $parsed } catch { }
                Show-CdoVersionStatus -Root $root -From (Get-ParsedOption -Parsed $parsed -Name 'from' -Default '')
            } else {
                Write-Host $script:CdoVersion
            }
        }
        'self' {
            if ($commandArgs.Count -lt 1) { Show-SelfHelp; break }
            $sub = ([string]$commandArgs[0]).ToLowerInvariant()
            switch ($sub) {
                'status' {
                    $root = $null
                    try { $root = Resolve-CdoSelfTargetRoot -Parsed $parsed } catch { }
                    Show-CdoVersionStatus -Root $root -From (Get-ParsedOption -Parsed $parsed -Name 'from' -Default '')
                }
                'version' {
                    $root = $null
                    try { $root = Resolve-CdoSelfTargetRoot -Parsed $parsed } catch { }
                    Show-CdoVersionStatus -Root $root -From (Get-ParsedOption -Parsed $parsed -Name 'from' -Default '')
                }
                'upgrade' {
                    Invoke-CdoSelfUpgrade `
                        -Root (Resolve-CdoSelfTargetRoot -Parsed $parsed) `
                        -From (Get-ParsedOption -Parsed $parsed -Name 'from' -Default '')
                }
                default { Show-SelfHelp }
            }
        }
        'cache' {
            $sub = if ($commandArgs.Count -gt 0) { ([string]$commandArgs[0]).ToLowerInvariant() } else { 'path' }
            switch ($sub) {
                'path' { Write-Host (Get-CdoGlobalCacheRoot) }
                'list' {
                    $root = Get-CdoGlobalCacheRoot
                    Write-Host "User cache: $root"
                    if (-not (Test-Path -LiteralPath $root)) {
                        Write-Host "  (empty)"
                    } else {
                        $files = @(Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction SilentlyContinue)
                        if ($files.Count -eq 0) {
                            Write-Host "  (empty)"
                        } else {
                            foreach ($file in $files) {
                                Write-Host ("  {0,10:n0}  {1}" -f $file.Length, (Get-CdoRelativePath -From $root -To $file.FullName))
                            }
                        }
                    }
                }
                default { Write-Host "Usage: cdo cache <path|list>" }
            }
        }
        'skeletons' { Show-Skeletons }
        'skeleton' {
            if ($commandArgs.Count -lt 1) { Show-Skeletons; break }
            $sub = ([string]$commandArgs[0]).ToLowerInvariant()
            if ($sub -eq 'list') {
                Show-Skeletons
            } elseif ($sub -eq 'apply' -or $sub -eq 'upgrade' -or $sub -eq 'update') {
                if ($commandArgs.Count -lt 2) { throw "Usage: cdo skeleton apply <name> [--force]" }
                Apply-CdoSkeletonToProject `
                    -Root (Get-ProjectRootForCli -Parsed $parsed) `
                    -Skeleton $commandArgs[1] `
                    -Force:(Get-ParsedFlag -Parsed $parsed -Name 'force')
            } else {
                Apply-CdoSkeletonToProject `
                    -Root (Get-ProjectRootForCli -Parsed $parsed) `
                    -Skeleton $commandArgs[0] `
                    -Force:(Get-ParsedFlag -Parsed $parsed -Name 'force')
            }
        }
        'init' {
            if (Get-ParsedFlag -Parsed $parsed -Name 'list-skeletons') {
                Show-Skeletons
                break
            }
            $path = if ($commandArgs.Count -gt 0) { $commandArgs[0] } else { '.' }
            $skeletonArg = Get-ParsedOption -Parsed $parsed -Name 'skeleton' -Default ''
            if ([string]::IsNullOrWhiteSpace($skeletonArg)) {
                # New minimal init: only common files, no skeleton, no targets
                Initialize-ProjectMinimal `
                    -Path $path `
                    -Name (Get-ParsedOption -Parsed $parsed -Name 'name' -Default '') `
                    -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') `
                    -Force:(Get-ParsedFlag -Parsed $parsed -Name 'force') `
                    -NoSelf:(Get-ParsedFlag -Parsed $parsed -Name 'no-self')
            } else {
                # Legacy skeleton-based init
                Initialize-Project `
                    -Path $path `
                    -Name (Get-ParsedOption -Parsed $parsed -Name 'name' -Default '') `
                    -Skeleton $skeletonArg `
                    -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') `
                    -Force:(Get-ParsedFlag -Parsed $parsed -Name 'force') `
                    -NoSelf:(Get-ParsedFlag -Parsed $parsed -Name 'no-self') `
                    -InstallTools:(Get-ParsedFlag -Parsed $parsed -Name 'install-tools')
            }
        }
        'doctor' {
            Invoke-Doctor -Root (Get-ProjectRootForCli -Parsed $parsed) -Fix:(Get-ParsedFlag -Parsed $parsed -Name 'fix')
        }
        'check' {
            Invoke-Doctor -Root (Get-ProjectRootForCli -Parsed $parsed) -Fix:(Get-ParsedFlag -Parsed $parsed -Name 'fix')
        }
        'activate' {
            Show-ActivationHelp -Root (Get-ProjectRootForCli -Parsed $parsed)
        }
        'sync' {
            $root = Get-ProjectRootForCli -Parsed $parsed
            $config = Load-Config -Root $root
            Sync-CdoProject -Root $root -Config $config -RefreshLayouts -Self
            if ((Get-ParsedFlag -Parsed $parsed -Name 'sources') -or (Get-ParsedFlag -Parsed $parsed -Name 'all')) {
                Sync-CdoSources -Root $root -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run')
            }
            if ((Get-ParsedFlag -Parsed $parsed -Name 'shaders') -or (Get-ParsedFlag -Parsed $parsed -Name 'all')) {
                $hasShaderConfig = $null -ne (Get-CdoProperty -Object $config -Name 'shaderToolchain')
                $hasShaderDir = Test-Path -LiteralPath (Join-Path $root 'assets\shaders\src')
                if ((Get-ParsedFlag -Parsed $parsed -Name 'shaders') -or $hasShaderConfig -or $hasShaderDir) {
                    Sync-CdoShaders -Root $root -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run')
                }
            }
        }
        'source' {
            if ($commandArgs.Count -lt 1) { Show-CdoSourceHelp; break }
            $sub = ([string]$commandArgs[0]).ToLowerInvariant()
            $root = Get-ProjectRootForCli -Parsed $parsed
            $sourceArgs = @()
            if ($commandArgs.Count -gt 1) { $sourceArgs = @($commandArgs[1..($commandArgs.Count - 1)]) }
            switch ($sub) {
                'list' {
                    Show-CdoSources `
                        -Root $root `
                        -Target (Get-ParsedOption -Parsed $parsed -Name 'target' -Default '') `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '')
                }
                'include' {
                    Add-CdoSources `
                        -Root $root `
                        -Files $sourceArgs `
                        -Target (Get-ParsedOption -Parsed $parsed -Name 'target' -Default '') `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '') `
                        -All:(Get-ParsedFlag -Parsed $parsed -Name 'all') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run')
                }
                'add' {
                    Add-CdoSources `
                        -Root $root `
                        -Files $sourceArgs `
                        -Target (Get-ParsedOption -Parsed $parsed -Name 'target' -Default '') `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '') `
                        -All:(Get-ParsedFlag -Parsed $parsed -Name 'all') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run')
                }
                'exclude' {
                    Remove-CdoSources `
                        -Root $root `
                        -Files $sourceArgs `
                        -Target (Get-ParsedOption -Parsed $parsed -Name 'target' -Default '') `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '') `
                        -All:(Get-ParsedFlag -Parsed $parsed -Name 'all') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run')
                }
                'cleanup' {
                    Remove-CdoMissingSources `
                        -Root $root `
                        -Target (Get-ParsedOption -Parsed $parsed -Name 'target' -Default '') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') | Out-Null
                }
                'prune' {
                    Remove-CdoMissingSources `
                        -Root $root `
                        -Target (Get-ParsedOption -Parsed $parsed -Name 'target' -Default '') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') | Out-Null
                }
                'sync' {
                    Sync-CdoSources `
                        -Root $root `
                        -Target (Get-ParsedOption -Parsed $parsed -Name 'target' -Default '') `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run')
                }
                'remove' {
                    Remove-CdoSources `
                        -Root $root `
                        -Files $sourceArgs `
                        -Target (Get-ParsedOption -Parsed $parsed -Name 'target' -Default '') `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '') `
                        -All:(Get-ParsedFlag -Parsed $parsed -Name 'all') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run')
                }
                default { Show-CdoSourceHelp }
            }
        }
        'sources' {
            if ($commandArgs.Count -lt 1) {
                Show-CdoSources -Root (Get-ProjectRootForCli -Parsed $parsed)
            } else {
                throw "Use 'cdo source <list|include|exclude>'."
            }
        }
        'tool' {
            if ($commandArgs.Count -lt 1) { Show-ToolHelp; break }
            $sub = ([string]$commandArgs[0]).ToLowerInvariant()
            $root = $null
            if ($sub -eq 'install') {
                $root = Get-ProjectRootForCli -Parsed $parsed
            } else {
                try { $root = Get-ProjectRootForCli -Parsed $parsed } catch { }
            }

            switch ($sub) {
                'list' { Show-CdoTools -Root $root }
                'ls' { Show-CdoTools -Root $root }
                'doctor' { Invoke-CdoToolDoctor -Root $root }
                'check' { Invoke-CdoToolDoctor -Root $root }
                'install' {
                    if ($commandArgs.Count -lt 2) { throw "Usage: cdo tool install <name> [--version <tag|latest-stable>] [--force] [--dry-run] [--refresh-cache]" }
                    Install-CdoTool `
                        -Root $root `
                        -Name $commandArgs[1] `
                        -Version (Get-ParsedOption -Parsed $parsed -Name 'version' -Default '') `
                        -Force:(Get-ParsedFlag -Parsed $parsed -Name 'force') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') `
                        -RefreshCache:(Get-ParsedFlag -Parsed $parsed -Name 'refresh-cache')
                }
                default { Show-ToolHelp }
            }
        }
        'add' {
            if ($commandArgs.Count -lt 1) { throw "Usage: cdo add <name> [--archive <path>] [--force]" }
            Add-Dependency `
                -Name $commandArgs[0] `
                -Root (Get-ProjectRootForCli -Parsed $parsed) `
                -ArchivePath (Get-ParsedOption -Parsed $parsed -Name 'archive' -Default '') `
                -Force:(Get-ParsedFlag -Parsed $parsed -Name 'force') `
                -RefreshCache:(Get-ParsedFlag -Parsed $parsed -Name 'refresh-cache')
        }
        'remove' {
            if ($commandArgs.Count -lt 1) { throw "Usage: cdo remove <name> [--delete-files]" }
            Remove-Dependency `
                -Name $commandArgs[0] `
                -Root (Get-ProjectRootForCli -Parsed $parsed) `
                -DeleteFiles:(Get-ParsedFlag -Parsed $parsed -Name 'delete-files')
        }
        'list' {
            $root = $null
            try { $root = Get-ProjectRootForCli -Parsed $parsed } catch { }
            List-Dependencies -Root $root
        }
        'deps' {
            $root = $null
            try { $root = Get-ProjectRootForCli -Parsed $parsed } catch { }
            List-Dependencies -Root $root
        }
        'build' {
            $app = if ($commandArgs.Count -gt 0) { $commandArgs[0] } else { '' }
            Build-App -Root (Get-ProjectRootForCli -Parsed $parsed) -Parsed $parsed -AppName $app
        }
        'run' {
            $app = if ($commandArgs.Count -gt 0) { $commandArgs[0] } else { '' }
            $extra = @()
            if ($commandArgs.Count -gt 1) { $extra = @($commandArgs[1..($commandArgs.Count - 1)]) }
            Run-App -Root (Get-ProjectRootForCli -Parsed $parsed) -Parsed $parsed -AppName $app -ExtraArgs $extra
        }
        'test' {
            $testTarget = if ($commandArgs.Count -gt 0) { $commandArgs[0] } else { '' }
            Run-Tests -Root (Get-ProjectRootForCli -Parsed $parsed) -Parsed $parsed -TargetName $testTarget
        }
        'clean' {
            Clean-Project -Root (Get-ProjectRootForCli -Parsed $parsed)
        }
        'target' {
            $subCommand = if ($commandArgs.Count -gt 0) { ([string]$commandArgs[0]).ToLowerInvariant() } else { '' }
            $root = Get-ProjectRootForCli -Parsed $parsed

            switch ($subCommand) {
                'list' {
                    Invoke-CdoTargetList -Root $root
                }
                'create' {
                    $name = if ($commandArgs.Count -gt 1) { $commandArgs[1] } else { '' }
                    if ([string]::IsNullOrWhiteSpace($name)) { throw "Usage: cdo target create <name> --type <executable|library> [--static|--shared]" }
                    $type = Get-ParsedOption -Parsed $parsed -Name 'type' -Default ''
                    if ([string]::IsNullOrWhiteSpace($type)) { throw "Usage: cdo target create <name> --type <executable|library> [--static|--shared]" }
                    $linkage = ''
                    if (Get-ParsedFlag -Parsed $parsed -Name 'static') { $linkage = 'static' }
                    if (Get-ParsedFlag -Parsed $parsed -Name 'shared') { $linkage = 'shared' }
                    $skeleton = Get-ParsedOption -Parsed $parsed -Name 'skeleton' -Default ''
                    Invoke-CdoTargetCreate -Root $root -Name $name -Type $type -Linkage $linkage -Skeleton $skeleton
                }
                'delete' {
                    $name = if ($commandArgs.Count -gt 1) { $commandArgs[1] } else { '' }
                    if ([string]::IsNullOrWhiteSpace($name)) { throw "Usage: cdo target delete <name>" }
                    Invoke-CdoTargetDelete -Root $root -Name $name
                }
                default {
                    Write-Host "Target commands:"
                    Write-Host "  cdo target list"
                    Write-Host "  cdo target create <name> --type <executable|library> [--static|--shared] [--skeleton <name>]"
                    Write-Host "  cdo target delete <name>"
                }
            }
        }
        'shader' {
            if ($commandArgs.Count -lt 1) { Show-ShaderHelp; break }
            $root = Get-ProjectRootForCli -Parsed $parsed
            $sub = ([string]$commandArgs[0]).ToLowerInvariant()
            $shaderArgs = @()
            if ($commandArgs.Count -gt 1) { $shaderArgs = @($commandArgs[1..($commandArgs.Count - 1)]) }
            switch ($sub) {
                'add' {
                    if ($commandArgs.Count -lt 3) { throw "Usage: cdo shader add <vertex|fragment|compute> <name> [--force]" }
                    Add-CdoShader -Root $root -Stage $commandArgs[1] -Name $commandArgs[2] -Force:(Get-ParsedFlag -Parsed $parsed -Name 'force')
                }
                'list' { Show-CdoShaders -Root $root }
                'ls' { Show-CdoShaders -Root $root }
                'include' {
                    Add-CdoShaderSources `
                        -Root $root `
                        -Files $shaderArgs `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '') `
                        -All:(Get-ParsedFlag -Parsed $parsed -Name 'all') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') | Out-Null
                }
                'exclude' {
                    Remove-CdoShaderSources `
                        -Root $root `
                        -Files $shaderArgs `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '') `
                        -All:(Get-ParsedFlag -Parsed $parsed -Name 'all') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') | Out-Null
                }
                'remove' {
                    Remove-CdoShaderSources `
                        -Root $root `
                        -Files $shaderArgs `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '') `
                        -All:(Get-ParsedFlag -Parsed $parsed -Name 'all') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') | Out-Null
                }
                'cleanup' { Remove-CdoMissingShaders -Root $root -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') | Out-Null }
                'prune' { Remove-CdoMissingShaders -Root $root -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') | Out-Null }
                'sync' {
                    Sync-CdoShaders `
                        -Root $root `
                        -Pattern (Get-ParsedOption -Parsed $parsed -Name 'pattern' -Default '') `
                        -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run')
                }
                'doctor' { Invoke-CdoShaderDoctor -Root $root }
                'compile' { Invoke-CdoShaderCompile -Root $root -DryRun:(Get-ParsedFlag -Parsed $parsed -Name 'dry-run') }
                'clean' { Clear-CdoShaderOutputs -Root $root }
                default { Show-ShaderHelp }
            }
        }
        default {
            throw "Unknown command: $command"
        }
    }
} catch {
    Write-CdoLog -Level ERROR -Message $_.Exception.Message
    Debug-Log $_.ScriptStackTrace
    exit 1
}
