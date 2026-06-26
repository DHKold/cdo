# build.ps1 — Direct GCC build script for CDo
# Usage:
#   .\build.ps1              Build the cdo executable
#   .\build.ps1 -Test        Build and run tests (cdo_pbt)
#   .\build.ps1 -Clean       Remove build artifacts
#   .\build.ps1 -All         Build both cdo and tests
#
# This script uses the bundled w64devkit GCC and does NOT require cdo.exe.

param(
    [switch]$Test,
    [switch]$Clean,
    [switch]$All
)

$ErrorActionPreference = "Continue"

# --- Configuration ---
$Root       = $PSScriptRoot
$ToolBin    = Join-Path $Root ".cdo\tools\w64devkit\bin"
$GCC        = Join-Path $ToolBin "gcc.exe"
$GPP        = Join-Path $ToolBin "g++.exe"
$BuildDir   = Join-Path $Root "build\debug"
$CdoBuild   = Join-Path $BuildDir "cdo"
$TestBuild  = Join-Path $BuildDir "cdo_pbt"

# Ensure w64devkit bin is on PATH (gcc needs to find 'as', 'ld', etc.)
if ($env:PATH -notlike "*$ToolBin*") {
    $env:PATH = "$ToolBin;$env:PATH"
}

$CFlags     = @("-std=c17", "-Wall", "-Wextra", "-Wno-unused-function", "-g", "-DDEBUG")
$CppFlags   = @("-std=c++20", "-Wall", "-Wextra", "-g", "-DDEBUG")
$LinkLibs   = @("-lwinhttp")

# --- Source Directories ---
$CdoSrc     = Join-Path $Root "src\cdo"
$TestSrc    = Join-Path $Root "tests"

# --- Clean ---
if ($Clean) {
    Write-Host "Cleaning build directory..."
    if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
    Write-Host "Clean complete."
    exit 0
}

# --- Helper: Compile a single file ---
function Compile-File {
    param(
        [string]$File,
        [string]$OutDir,
        [string[]]$IncludePaths,
        [string[]]$ExtraFlags,
        [string]$ObjName
    )

    if (!(Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

    $ext = [System.IO.Path]::GetExtension($File)
    $obj = Join-Path $OutDir $ObjName
    $includes = @()
    foreach ($p in $IncludePaths) { $includes += "-I"; $includes += $p }

    if ($ext -eq ".cpp" -or $ext -eq ".cxx" -or $ext -eq ".cc") {
        $args = $CppFlags + $includes + $ExtraFlags + @("-c", $File, "-o", $obj)
        & $GPP @args
    } else {
        $args = $CFlags + $includes + $ExtraFlags + @("-c", $File, "-o", $obj)
        & $GCC @args
    }

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Compilation failed: $File"
        exit 1
    }
    return $obj
}

# --- Helper: Get all C/C++ source files recursively ---
function Get-Sources {
    param([string]$Dir, [string[]]$Exclude)
    $files = Get-ChildItem -Path $Dir -Recurse -Include "*.c","*.cpp","*.cxx","*.cc"
    if ($Exclude -and $Exclude.Count -gt 0) {
        $pattern = ($Exclude -join '|')
        $files = $files | Where-Object { $_.FullName -notmatch $pattern }
    }
    return $files
}

# --- Build CDo Executable ---
function Build-Cdo {
    Write-Host "`n=== Building CDo ===" -ForegroundColor Cyan
    if (!(Test-Path $CdoBuild)) { New-Item -ItemType Directory -Path $CdoBuild -Force | Out-Null }

    $sources = Get-Sources -Dir $CdoSrc -Exclude @()
    $objects = @()

    foreach ($f in $sources) {
        $rel = $f.FullName.Substring($CdoSrc.Length + 1) -replace '[/\\]', '_'
        $objName = $rel -replace '\.(c|cpp|cxx|cc)$', '.o'
        Write-Host "  CC $($f.Name)"
        $obj = Compile-File -File $f.FullName -OutDir $CdoBuild `
            -IncludePaths @($CdoSrc) -ObjName $objName
        $objects += $obj
    }

    $exe = Join-Path $CdoBuild "cdo.exe"
    Write-Host "  LINK cdo.exe"
    $linkArgs = $objects + @("-o", $exe) + $LinkLibs
    & $GPP @linkArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Linking failed"
        exit 1
    }

    Write-Host "Build complete: $exe" -ForegroundColor Green

    # Deploy catalog files alongside the binary
    $catalogSrc = Join-Path $Root "catalogs"
    $catalogDst = Join-Path $CdoBuild "catalogs"
    if (Test-Path $catalogSrc) {
        if (!(Test-Path $catalogDst)) { New-Item -ItemType Directory -Path $catalogDst -Force | Out-Null }
        $tomlFiles = Get-ChildItem -Path $catalogSrc -Filter "*.toml"
        foreach ($f in $tomlFiles) {
            Copy-Item -Path $f.FullName -Destination $catalogDst -Force
        }
        Write-Host "  Deployed $($tomlFiles.Count) catalog file(s) to $catalogDst" -ForegroundColor Yellow
    }

    return $exe
}

# --- Build Tests ---
function Build-Tests {
    Write-Host "`n=== Building Tests (cdo_pbt) ===" -ForegroundColor Cyan
    if (!(Test-Path $TestBuild)) { New-Item -ItemType Directory -Path $TestBuild -Force | Out-Null }

    # Include paths: tests/ and src/cdo/ (for pal.h, core/*.h, etc.)
    $includes = @($TestSrc, $CdoSrc)
    $objects = @()

    # 1. Compile CDo source files (excluding main.cpp — the test has its own main)
    $cdoSources = Get-Sources -Dir $CdoSrc -Exclude @("main\.cpp$")
    foreach ($f in $cdoSources) {
        $rel = $f.FullName.Substring($CdoSrc.Length + 1) -replace '[/\\]', '_'
        $objName = "cdo_" + ($rel -replace '\.(c|cpp|cxx|cc)$', '.o')
        Write-Host "  CC $($f.Name) (cdo)"
        $obj = Compile-File -File $f.FullName -OutDir $TestBuild `
            -IncludePaths $includes -ExtraFlags @("-DCDO_TESTING") -ObjName $objName
        $objects += $obj
    }

    # 2. Compile test source files (only test_main.c — skip test_cdo.cpp which has conflicting main)
    $testFile = Join-Path $TestSrc "test_main.c"
    Write-Host "  CC test_main.c"
    $obj = Compile-File -File $testFile -OutDir $TestBuild `
        -IncludePaths $includes -ExtraFlags @("-DCDO_TESTING") -ObjName "test_main.o"
    $objects += $obj

    # 3. Link
    $exe = Join-Path $TestBuild "cdo_pbt.exe"
    Write-Host "  LINK cdo_pbt.exe"
    $linkArgs = $objects + @("-o", $exe) + $LinkLibs
    & $GPP @linkArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Linking failed"
        exit 1
    }

    Write-Host "Build complete: $exe" -ForegroundColor Green
    return $exe
}

# --- Main ---
if ($All) {
    Build-Cdo
    $testExe = Build-Tests
    Write-Host "`n=== Running Tests ===" -ForegroundColor Cyan
    & $testExe
    exit $LASTEXITCODE
}
elseif ($Test) {
    $testExe = Build-Tests
    Write-Host "`n=== Running Tests ===" -ForegroundColor Cyan
    & $testExe
    exit $LASTEXITCODE
}
else {
    Build-Cdo
}
