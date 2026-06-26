#!/usr/bin/env pwsh
# bootstrap.ps1 — Bootstrap CDo from source
#
# This script builds cdo.exe from source using either:
#   1. A system GCC/Clang already on PATH
#   2. A vendored w64devkit in .cdo/tools/ (downloaded if needed)
#
# Usage: .\bootstrap.ps1

$ErrorActionPreference = "Stop"
$CDO_ROOT = $PSScriptRoot
$TOOLS_DIR = Join-Path $CDO_ROOT ".cdo" "tools"
$W64DEVKIT_DIR = Join-Path $TOOLS_DIR "w64devkit"
$W64DEVKIT_BIN = Join-Path $W64DEVKIT_DIR "bin"
$W64DEVKIT_URL = "https://github.com/skeeto/w64devkit/releases/download/v2.0.0/w64devkit-x64-2.0.0.zip"

function Find-Compiler {
    # Check PATH first
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if ($gcc) { return $gcc.Source }
    
    $clang = Get-Command clang -ErrorAction SilentlyContinue
    if ($clang) { return $clang.Source }
    
    # Check vendored
    $vendored = Join-Path $W64DEVKIT_BIN "gcc.exe"
    if (Test-Path $vendored) { return $vendored }
    
    return $null
}

function Install-W64DevKit {
    Write-Host "No compiler found. Downloading w64devkit..." -ForegroundColor Yellow
    
    New-Item -ItemType Directory -Path $TOOLS_DIR -Force | Out-Null
    $zipPath = Join-Path $TOOLS_DIR "w64devkit.zip"
    
    Write-Host "Downloading from $W64DEVKIT_URL..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $W64DEVKIT_URL -OutFile $zipPath -UseBasicParsing
    
    Write-Host "Extracting..."
    Expand-Archive -Path $zipPath -DestinationPath $TOOLS_DIR -Force
    Remove-Item $zipPath -Force
    
    Write-Host "w64devkit installed." -ForegroundColor Green
}

# --- Main ---
Write-Host ""
Write-Host "  CDo Bootstrap" -ForegroundColor Cyan
Write-Host "  =============" -ForegroundColor Cyan
Write-Host ""

$compiler = Find-Compiler
if (-not $compiler) {
    if ($IsLinux -or $IsMacOS) {
        Write-Host "ERROR: No C compiler found on PATH. Install gcc or clang." -ForegroundColor Red
        exit 1
    }
    Install-W64DevKit
    $compiler = Join-Path $W64DEVKIT_BIN "gcc.exe"
}

Write-Host "Using compiler: $compiler" -ForegroundColor Green

# Determine compiler directory for PATH (needed for as, ld, etc.)
$compilerDir = Split-Path $compiler -Parent
$env:PATH = "$compilerDir;$env:PATH"

# Collect all .c sources
$srcDir = Join-Path $CDO_ROOT "src" "cdo"
$cSources = Get-ChildItem -Path $srcDir -Recurse -Filter "*.c" | ForEach-Object { $_.FullName }
$cppSources = Get-ChildItem -Path $srcDir -Recurse -Filter "*.cpp" | ForEach-Object { $_.FullName }

# Build output
$buildDir = Join-Path $CDO_ROOT "build" "bootstrap"
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

$objFiles = @()

# Compile C sources
Write-Host ""
Write-Host "Compiling $($cSources.Count) C files + $($cppSources.Count) C++ files..."

foreach ($src in $cSources) {
    $relPath = $src.Substring($srcDir.Length + 1) -replace '[/\\]', '_'
    $obj = Join-Path $buildDir ($relPath -replace '\.c$', '.o')
    
    $args = @("-c", "-std=c17", "-I$srcDir", "-o", $obj, $src)
    $proc = Start-Process -FilePath "gcc" -ArgumentList $args -NoNewWindow -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        Write-Host "ERROR: Failed to compile $src" -ForegroundColor Red
        exit 1
    }
    $objFiles += $obj
}

foreach ($src in $cppSources) {
    $relPath = $src.Substring($srcDir.Length + 1) -replace '[/\\]', '_'
    $obj = Join-Path $buildDir ($relPath -replace '\.cpp$', '.o')
    
    $args = @("-c", "-std=c++20", "-I$srcDir", "-o", $obj, $src)
    $proc = Start-Process -FilePath "g++" -ArgumentList $args -NoNewWindow -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        Write-Host "ERROR: Failed to compile $src" -ForegroundColor Red
        exit 1
    }
    $objFiles += $obj
}

# Link
$output = Join-Path $CDO_ROOT "cdo.exe"
Write-Host "Linking $($objFiles.Count) objects -> cdo.exe"

$linkArgs = @($objFiles) + @("-o", $output, "-lwinhttp", "-static")
$proc = Start-Process -FilePath "g++" -ArgumentList $linkArgs -NoNewWindow -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    Write-Host "ERROR: Linking failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "  Bootstrap complete!" -ForegroundColor Green
Write-Host "  Binary: $output" -ForegroundColor Green
Write-Host ""
Write-Host "  You can now use: .\cdo.exe build cdo" -ForegroundColor Cyan
Write-Host ""
