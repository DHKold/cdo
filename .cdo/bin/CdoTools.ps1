<#
Portable developer tool installation for cdo projects.
#>

function Get-CdoToolCatalog {
    # Load tool catalog from tools-catalog.json (data-driven, extensible)
    $catalogFile = Join-Path $PSScriptRoot 'tools-catalog.json'
    if (-not (Test-Path -LiteralPath $catalogFile)) {
        throw "tools-catalog.json not found next to CdoTools.ps1 ($PSScriptRoot)."
    }

    $json = Get-Content -LiteralPath $catalogFile -Raw | ConvertFrom-Json
    $catalog = [ordered]@{}
    foreach ($prop in $json.PSObject.Properties) {
        $entry = [ordered]@{}
        foreach ($field in $prop.Value.PSObject.Properties) {
            $val = $field.Value
            # Convert JSON arrays to PowerShell arrays
            if ($val -is [System.Collections.IEnumerable] -and -not ($val -is [string])) {
                $val = @($val)
            }
            $entry[$field.Name] = $val
        }
        $catalog[$prop.Name] = $entry
    }
    return $catalog
}

function Resolve-CdoToolName {
    param([string]$Name)

    $key = $Name.ToLowerInvariant()

    # Static aliases for common alternate names
    $aliases = @{
        'dxcompiler'               = 'dxc'
        'directxshadercompiler'    = 'dxc'
        'directx-shader-compiler'  = 'dxc'
        'vulkan'                   = 'glslang'
        'vulkan-sdk'               = 'glslang'
        'glslangvalidator'         = 'glslang'
        'sdl-shadercross'          = 'sdl_shadercross'
        'ninja-build'              = 'ninja'
    }

    if ($aliases.ContainsKey($key)) { return $aliases[$key] }

    $catalog = Get-CdoToolCatalog
    if ($catalog.Contains($key)) { return $key }
    $valid = ($catalog.Keys | Sort-Object) -join ', '
    throw "Unknown tool '$Name'. Available tools: $valid"
}

function Get-CdoToolsRoot {
    param([string]$Root)
    return Join-Path $Root '.cdo\tools'
}

function Get-CdoToolRoot {
    param([string]$Root, [string]$Name)
    return Join-Path (Get-CdoToolsRoot -Root $Root) (Resolve-CdoToolName -Name $Name)
}

function Get-CdoToolManifestPath {
    param([string]$Root, [string]$Name)
    return Join-Path (Get-CdoToolRoot -Root $Root -Name $Name) 'cdo-tool.json'
}

function Get-CdoToolManifest {
    param([string]$Root, [string]$Name)
    return Read-CdoJson -Path (Get-CdoToolManifestPath -Root $Root -Name $Name)
}

function Get-CdoToolBinDirectories {
    param([string]$Root)

    $dirs = New-Object 'System.Collections.Generic.List[string]'
    $toolsRoot = Get-CdoToolsRoot -Root $Root
    if (-not (Test-Path -LiteralPath $toolsRoot)) { return @() }

    foreach ($manifestFile in @(Get-ChildItem -LiteralPath $toolsRoot -Recurse -Filter 'cdo-tool.json' -ErrorAction SilentlyContinue)) {
        $manifest = Read-CdoJson -Path $manifestFile.FullName
        $toolRoot = Split-Path -Parent $manifestFile.FullName
        foreach ($dir in @(Get-CdoProperty -Object $manifest -Name 'binDirs' -Default @())) {
            $abs = Join-Path $toolRoot ([string]$dir)
            if ((Test-Path -LiteralPath $abs) -and -not $dirs.Contains($abs)) {
                $dirs.Add($abs)
            }
        }
    }

    return $dirs.ToArray()
}

function Add-CdoToolPathForProcess {
    param([string]$Root)

    $dirs = @(Get-CdoToolBinDirectories -Root $Root)
    foreach ($dir in $dirs) {
        if (($env:PATH -split ';') -notcontains $dir) {
            $env:PATH = "$dir;$env:PATH"
        }
    }
}

function Find-CdoToolCommand {
    param(
        [string]$Root,
        [string]$Command
    )

    if ($Root) {
        foreach ($dir in @(Get-CdoToolBinDirectories -Root $Root)) {
            foreach ($name in @($Command, "$Command.exe", "$Command.cmd", "$Command.bat")) {
                $candidate = Join-Path $dir $name
                if (Test-Path -LiteralPath $candidate) { return $candidate }
            }
        }

        $shim = Join-Path $Root ".cdo\bin\$Command.cmd"
        if (Test-Path -LiteralPath $shim) { return $shim }
    }

    $cmd = Get-Command $Command -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Find-CdoInstalledToolExecutable {
    param(
        [string]$Root,
        [string]$ToolName,
        [string]$Command
    )

    $toolName = Resolve-CdoToolName -Name $ToolName
    $toolRoot = Get-CdoToolRoot -Root $Root -Name $toolName
    if (-not (Test-Path -LiteralPath $toolRoot)) { return $null }

    $catalog = Get-CdoToolCatalog
    $entry = $catalog[$toolName]
    $binHints = @(Get-CdoProperty -Object $entry -Name 'binHints' -Default @(''))
    $names = @("$Command.exe", $Command, "$Command.cmd", "$Command.bat")

    foreach ($hint in $binHints) {
        foreach ($name in $names) {
            $candidate = Join-Path (Join-Path $toolRoot ([string]$hint)) $name
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    $matches = @()
    foreach ($name in $names) {
        $matches += @(Get-ChildItem -LiteralPath $toolRoot -Recurse -File -Filter $name -ErrorAction SilentlyContinue)
    }

    if ($matches.Count -eq 0) { return $null }

    foreach ($hint in $binHints) {
        if ([string]::IsNullOrWhiteSpace([string]$hint)) { continue }
        $normalizedHint = ([string]$hint).Replace('/', '\')
        $match = @($matches | Where-Object { $_.FullName.Replace('/', '\') -like "*\$normalizedHint\*" } | Select-Object -First 1)
        if ($match.Count -gt 0) { return $match[0].FullName }
    }

    return $matches[0].FullName
}

function Write-CdoToolShim {
    param(
        [string]$Root,
        [string]$ToolName,
        [string]$Command,
        [string]$ExecutablePath
    )

    $binDir = Join-Path $Root '.cdo\bin'
    New-CdoDirectory -Path $binDir
    $toolRoot = Get-CdoToolRoot -Root $Root -Name $ToolName
    $relativeExe = Get-CdoRelativePath -From $binDir -To $ExecutablePath
    $shim = @"
@echo off
set "SCRIPT_DIR=%~dp0"
"%SCRIPT_DIR%$relativeExe" %*
"@
    Write-CdoTextFile -Path (Join-Path $binDir "$Command.cmd") -Content $shim
}

function Get-GitHubRelease {
    param(
        [string]$Repo,
        [string]$Version
    )

    $headers = @{ 'User-Agent' = 'cdo-cli' }
    $url = if ([string]::IsNullOrWhiteSpace($Version) -or $Version -eq 'latest' -or $Version -eq 'latest-stable') {
        "https://api.github.com/repos/$Repo/releases"
    } else {
        "https://api.github.com/repos/$Repo/releases/tags/$Version"
    }

    if ($Version -eq 'latest-stable') {
        $releases = Invoke-RestMethod -Uri $url -Headers $headers -UseBasicParsing
        return @($releases | Where-Object { -not $_.prerelease -and -not $_.draft } | Select-Object -First 1)[0]
    }

    return Invoke-RestMethod -Uri $url -Headers $headers -UseBasicParsing
}

function Install-CdoTool {
    param(
        [string]$Root,
        [string]$Name,
        [string]$Version = '',
        [switch]$Force,
        [switch]$DryRun,
        [switch]$RefreshCache
    )

    if ([string]::IsNullOrWhiteSpace($Root)) {
        throw "Tool installation requires a cdo project. Run inside a project or pass --project <path>."
    }

    $toolName = Resolve-CdoToolName -Name $Name
    $catalog = Get-CdoToolCatalog
    $entry = $catalog[$toolName]
    $kind = [string](Get-CdoProperty -Object $entry -Name 'kind')
    $toolRoot = Get-CdoToolRoot -Root $Root -Name $toolName
    $manifest = Get-CdoToolManifest -Root $Root -Name $toolName

    if ($manifest -and -not $Force) {
        Write-CdoLog -Level OK -Message "$toolName already installed"
        return
    }

    if ($kind -eq 'manual') {
        Write-CdoLog -Level WARN -Message "$toolName is not automated yet"
        Write-Host (Get-CdoProperty -Object $entry -Name 'notes')
        return
    }

    if ($kind -ne 'github-release') {
        throw "Unsupported tool installer kind '$kind' for $toolName."
    }

    $repo = [string](Get-CdoProperty -Object $entry -Name 'repo')
    $wantedVersion = if ([string]::IsNullOrWhiteSpace($Version)) { [string](Get-CdoProperty -Object $entry -Name 'version' -Default 'latest-stable') } else { $Version }
    if ($DryRun) {
        Write-Host "Would resolve $repo release '$wantedVersion', cache the archive under $(Get-CdoGlobalCacheRoot), and install it under .cdo/tools/$toolName"
        return
    }

    $release = Get-GitHubRelease -Repo $repo -Version $wantedVersion
    if ($null -eq $release) { throw "Could not resolve GitHub release for $repo ($wantedVersion)." }

    $assetPattern = [string](Get-CdoProperty -Object $entry -Name 'assetPattern')
    $assets = @($release.assets | Where-Object { $_.name -match $assetPattern })
    if ($assets.Count -eq 0) {
        $available = @($release.assets | ForEach-Object { $_.name }) -join ', '
        throw "No release asset matched '$assetPattern'. Available assets: $available"
    }
    $asset = $assets[0]
    $downloadUrl = [string]$asset.browser_download_url
    $assetName = [string]$asset.name

    if ((Test-Path -LiteralPath $toolRoot) -and $Force) {
        Remove-Item -LiteralPath $toolRoot -Recurse -Force
    }
    New-CdoDirectory -Path $toolRoot
    $cacheDir = Join-Path $Root '.cdo\cache'
    New-CdoDirectory -Path $cacheDir
    $archive = Join-Path $cacheDir $assetName

    Write-CdoLog -Level INFO -Message "preparing $toolName $($release.tag_name)"
    Get-CdoCachedDownload -Url $downloadUrl -Destination $archive -FileName $assetName -Refresh:$RefreshCache | Out-Null
    Write-CdoLog -Level INFO -Message "extracting $assetName"
    Extract-CdoArchive -ArchivePath $archive -OutDir $toolRoot

    $commands = @(Get-CdoProperty -Object $entry -Name 'commands' -Default @($toolName))
    $binDirs = New-Object 'System.Collections.Generic.List[string]'
    foreach ($command in $commands) {
        $exePath = Find-CdoInstalledToolExecutable -Root $Root -ToolName $toolName -Command ([string]$command)
        $exe = if ($exePath) { Get-Item -LiteralPath $exePath } else { $null }
        if ($exe) {
            $dir = Split-Path -Parent $exe.FullName
            $relDir = Get-CdoRelativePath -From $toolRoot -To $dir
            if (-not $binDirs.Contains($relDir)) { $binDirs.Add($relDir) }
            Write-CdoToolShim -Root $Root -ToolName $toolName -Command $command -ExecutablePath $exe.FullName
        } else {
            Write-CdoLog -Level WARN -Message "installed $toolName, but could not find command '$command' in archive"
        }
    }

    $toolManifest = [ordered]@{
        name = $toolName
        displayName = [string](Get-CdoProperty -Object $entry -Name 'displayName' -Default $toolName)
        repo = $repo
        version = [string]$release.tag_name
        asset = $assetName
        installedAt = Get-CdoTimestamp
        commands = $commands
        binDirs = @($binDirs.ToArray())
    }
    Write-CdoJson -Path (Get-CdoToolManifestPath -Root $Root -Name $toolName) -Value $toolManifest
    Add-CdoToolPathForProcess -Root $Root
    Write-CdoLog -Level OK -Message "$toolName installed"
}

function Show-CdoTools {
    param([string]$Root)

    $catalog = Get-CdoToolCatalog
    Write-Host "Tool catalog:"
    foreach ($name in ($catalog.Keys | Sort-Object)) {
        $entry = $catalog[$name]
        Write-Host ("  {0,-16} {1}" -f $name, (Get-CdoProperty -Object $entry -Name 'displayName' -Default $name))
    }

    Write-Host ""
    Write-Host "Installed:"
    $manifests = @()
    $toolsRoot = ''
    if (-not [string]::IsNullOrWhiteSpace($Root)) {
        $toolsRoot = Get-CdoToolsRoot -Root $Root
    }
    if ($toolsRoot -and (Test-Path -LiteralPath $toolsRoot)) {
        $manifests = @(Get-ChildItem -LiteralPath $toolsRoot -Recurse -Filter 'cdo-tool.json' -ErrorAction SilentlyContinue)
    }
    if ($manifests.Count -eq 0) {
        Write-Host "  (none)"
        return
    }
    foreach ($file in $manifests) {
        $manifest = Read-CdoJson -Path $file.FullName
        Write-Host ("  {0,-16} {1}" -f (Get-CdoProperty -Object $manifest -Name 'name'), (Get-CdoProperty -Object $manifest -Name 'version'))
    }
}

function Invoke-CdoToolDoctor {
    param([string]$Root)

    $catalog = Get-CdoToolCatalog
    foreach ($name in ($catalog.Keys | Sort-Object)) {
        $entry = $catalog[$name]
        $commands = @(Get-CdoProperty -Object $entry -Name 'commands' -Default @($name))
        $foundAny = $false
        foreach ($command in $commands) {
            $found = Find-CdoToolCommand -Root $Root -Command $command
            if ($found) {
                Write-CdoLog -Level OK -Message "$command found: $found"
                $foundAny = $true
            }
        }
        if (-not $foundAny) {
            Write-CdoLog -Level WARN -Message "$name missing - $((Get-CdoProperty -Object $entry -Name 'notes'))"
        }
    }
}

function Install-CdoRequiredTools {
    param(
        [string]$Root,
        [object]$Config,
        [switch]$Force,
        [switch]$RefreshCache
    )

    $tools = @(Get-CdoProperty -Object $Config -Name 'requiredTools' -Default @())
    foreach ($tool in $tools) {
        Install-CdoTool -Root $Root -Name ([string]$tool) -Force:$Force -RefreshCache:$RefreshCache
    }
}
