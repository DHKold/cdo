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