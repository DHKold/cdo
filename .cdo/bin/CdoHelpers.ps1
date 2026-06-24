<#
Shared helpers for cdo.ps1.
#>

function Get-CdoTimestamp {
    return [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
}

function Write-CdoLog {
    param(
        [ValidateSet('INFO', 'OK', 'WARN', 'ERROR', 'DEBUG')]
        [string]$Level,
        [string]$Message
    )

    if ($Level -eq 'DEBUG') {
        $var = Get-Variable -Name CdoDebug -Scope Global -ErrorAction SilentlyContinue
        if (-not $var -or -not $Global:CdoDebug) { return }
    }

    $prefix = switch ($Level) {
        'OK'    { '[ok]   ' }
        'WARN'  { '[warn] ' }
        'ERROR' { '[err]  ' }
        'DEBUG' { '[dbg]  ' }
        default { '[info] ' }
    }

    $color = switch ($Level) {
        'OK'    { 'Green' }
        'WARN'  { 'Yellow' }
        'ERROR' { 'Red' }
        'DEBUG' { 'DarkGray' }
        default { 'Cyan' }
    }

    Write-Host "$prefix$Message" -ForegroundColor $color
}

function Debug-Log {
    param([string]$Message)
    Write-CdoLog -Level DEBUG -Message $Message
}

function Test-CdoWindows {
    return [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
}

function Resolve-CdoPath {
    param([string]$Path)
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Get-CdoUserRoot {
    if (-not [string]::IsNullOrWhiteSpace($env:CDO_HOME)) {
        return Resolve-CdoPath -Path $env:CDO_HOME
    }

    $userHome = $env:USERPROFILE
    if ([string]::IsNullOrWhiteSpace($userHome)) { $userHome = $HOME }
    return Join-Path $userHome '.cdo'
}

function Get-CdoGlobalCacheRoot {
    return Join-Path (Get-CdoUserRoot) 'cache'
}

function Get-CdoSafeCacheLeaf {
    param([string]$Url, [string]$FileName)

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Url)
        $hashBytes = $sha.ComputeHash($bytes)
    } finally {
        $sha.Dispose()
    }
    $hash = ([System.BitConverter]::ToString($hashBytes) -replace '-', '').Substring(0, 12).ToLowerInvariant()

    $leaf = $FileName
    if ([string]::IsNullOrWhiteSpace($leaf)) { $leaf = 'download.bin' }
    $invalid = [System.IO.Path]::GetInvalidFileNameChars()
    foreach ($char in $invalid) {
        $leaf = $leaf.Replace([string]$char, '_')
    }

    return "$hash-$leaf"
}

function Get-CdoCachedDownloadPath {
    param([string]$Url, [string]$FileName)

    return Join-Path (Join-Path (Get-CdoGlobalCacheRoot) 'downloads') (Get-CdoSafeCacheLeaf -Url $Url -FileName $FileName)
}

function New-CdoDirectory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Write-CdoTextFile {
    param(
        [string]$Path,
        [string]$Content
    )
    $parent = Split-Path -Parent $Path
    if ($parent) { New-CdoDirectory -Path $parent }
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $utf8)
}

function Write-CdoFileIfMissing {
    param(
        [string]$Path,
        [string]$Content
    )
    if (Test-Path -LiteralPath $Path) {
        Write-CdoLog -Level INFO -Message "kept existing $Path"
        return $false
    }

    Write-CdoTextFile -Path $Path -Content $Content
    Write-CdoLog -Level OK -Message "created $Path"
    return $true
}

function Read-CdoJson {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Write-CdoJson {
    param(
        [string]$Path,
        [object]$Value
    )
    $json = $Value | ConvertTo-Json -Depth 32
    Write-CdoTextFile -Path $Path -Content ($json + [Environment]::NewLine)
}

function Convert-CdoObjectToHashtable {
    param([object]$InputObject)

    if ($null -eq $InputObject) { return $null }

    if ($InputObject -is [System.Collections.IDictionary]) {
        $result = [ordered]@{}
        foreach ($key in $InputObject.Keys) {
            $result[$key] = Convert-CdoObjectToHashtable -InputObject $InputObject[$key]
        }
        return $result
    }

    if ($InputObject -is [System.Collections.IEnumerable] -and -not ($InputObject -is [string])) {
        $items = [System.Collections.ArrayList]::new()
        foreach ($item in $InputObject) {
            [void]$items.Add((Convert-CdoObjectToHashtable -InputObject $item))
        }
        return ,@($items)
    }

    if ($InputObject -is [pscustomobject]) {
        $result = [ordered]@{}
        foreach ($prop in $InputObject.PSObject.Properties) {
            $result[$prop.Name] = Convert-CdoObjectToHashtable -InputObject $prop.Value
        }
        return $result
    }

    return $InputObject
}

function Get-CdoProperty {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Default = $null
    )

    if ($null -eq $Object) { return $Default }

    if ($Object -is [System.Collections.IDictionary]) {
        if ($Object.Contains($Name)) { return $Object[$Name] }
        return $Default
    }

    $prop = $Object.PSObject.Properties[$Name]
    if ($prop) { return $prop.Value }
    return $Default
}

function Set-CdoProperty {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Value
    )

    if ($Object -is [System.Collections.IDictionary]) {
        $Object[$Name] = $Value
        return
    }

    $prop = $Object.PSObject.Properties[$Name]
    if ($prop) {
        $prop.Value = $Value
    } else {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

function Test-CdoCommand {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-CdoNative {
    param(
        [string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = (Get-Location).Path,
        [switch]$ThrowOnError
    )

    Debug-Log ("running: {0} {1}" -f $FilePath, ($Arguments -join ' '))
    $oldLocation = (Get-Location).Path
    $oldErrorActionPreference = $ErrorActionPreference
    $tail = New-Object 'System.Collections.Generic.Queue[string]'
    $tailLimit = 80
    try {
        if ($WorkingDirectory) { Set-Location -LiteralPath $WorkingDirectory }
        $ErrorActionPreference = 'Continue'
        & $FilePath @Arguments 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                $line = $_.Exception.Message
                if ([string]::IsNullOrWhiteSpace($line)) { $line = $_.ToString() }
            } else {
                $line = $_.ToString()
            }
            if ($line -eq 'System.Management.Automation.RemoteException') { $line = '' }
            if ($line.Length -gt 0) { Write-Host $line }
            $tail.Enqueue($line)
            while ($tail.Count -gt $tailLimit) { $tail.Dequeue() | Out-Null }
        }
        $exitCode = $LASTEXITCODE
        if ($null -eq $exitCode) { $exitCode = 0 }
    } catch {
        if ($ThrowOnError) { throw }
        Write-CdoLog -Level ERROR -Message $_.Exception.Message
        return 127
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
        Set-Location -LiteralPath $oldLocation
    }

    if ($exitCode -ne 0 -and $ThrowOnError) {
        if ($tail.Count -gt 0) {
            Write-CdoLog -Level ERROR -Message "last command output:"
            foreach ($line in $tail) {
                if ($line.Length -gt 0) { Write-Host "  $line" -ForegroundColor DarkGray }
            }
        }
        throw ("Command failed with exit code {0}: {1} {2}" -f $exitCode, $FilePath, ($Arguments -join ' '))
    }

    return $exitCode
}

function Run-Command {
    param(
        [string]$Cmd,
        [switch]$ThrowOnError
    )

    Debug-Log "legacy command: $Cmd"
    Invoke-Expression $Cmd
    $exitCode = $LASTEXITCODE
    if ($null -eq $exitCode) { $exitCode = 0 }
    if ($exitCode -ne 0 -and $ThrowOnError) {
        throw "Command failed with exit code $exitCode : $Cmd"
    }
    return $exitCode
}

function Download-File {
    param(
        [string]$Url,
        [string]$Destination
    )

    $parent = Split-Path -Parent $Destination
    if ($parent) { New-CdoDirectory -Path $parent }

    Debug-Log "downloading $Url -> $Destination"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing -ErrorAction Stop
    } catch {
        Write-CdoLog -Level ERROR -Message "download failed: $($_.Exception.Message)"
        throw
    }
}

function Get-CdoCachedDownload {
    param(
        [string]$Url,
        [string]$Destination,
        [string]$FileName = '',
        [switch]$Refresh
    )

    if ([string]::IsNullOrWhiteSpace($FileName)) {
        try {
            $uri = [System.Uri]$Url
            $FileName = [System.IO.Path]::GetFileName($uri.LocalPath)
        } catch {
            $FileName = [System.IO.Path]::GetFileName($Destination)
        }
    }
    if ([string]::IsNullOrWhiteSpace($FileName)) { $FileName = [System.IO.Path]::GetFileName($Destination) }

    $cachePath = Get-CdoCachedDownloadPath -Url $Url -FileName $FileName
    if ((-not (Test-Path -LiteralPath $cachePath)) -or $Refresh) {
        Write-CdoLog -Level INFO -Message "downloading to user cache: $FileName"
        Download-File -Url $Url -Destination $cachePath
    } else {
        Write-CdoLog -Level INFO -Message "using user cache: $FileName"
    }

    $parent = Split-Path -Parent $Destination
    if ($parent) { New-CdoDirectory -Path $parent }
    Copy-Item -LiteralPath $cachePath -Destination $Destination -Force
    return $Destination
}

function Extract-CdoArchive {
    param(
        [string]$ArchivePath,
        [string]$OutDir
    )

    if (-not (Test-Path -LiteralPath $ArchivePath)) {
        throw "Archive not found: $ArchivePath"
    }

    New-CdoDirectory -Path $OutDir
    $lower = $ArchivePath.ToLowerInvariant()
    Debug-Log "extracting $ArchivePath -> $OutDir"

    if ($lower.EndsWith('.zip')) {
        Expand-Archive -LiteralPath $ArchivePath -DestinationPath $OutDir -Force
        return
    }

    if ($lower.EndsWith('.tar.gz') -or $lower.EndsWith('.tgz') -or $lower.EndsWith('.tar.bz2') -or $lower.EndsWith('.tbz2') -or $lower.EndsWith('.tar')) {
        if (-not (Test-CdoCommand -Name 'tar')) {
            throw "tar is required to extract $ArchivePath"
        }
        $null = Invoke-CdoNative -FilePath 'tar' -Arguments @('-xf', $ArchivePath, '-C', $OutDir) -ThrowOnError
        return
    }

    throw "Unsupported archive format: $ArchivePath"
}

function Pause-For-Manual {
    param([string]$Message)
    Write-CdoLog -Level WARN -Message $Message
    Write-Host ""
    Read-Host "Press ENTER to continue" | Out-Null
    Write-Host ""
}

function Get-CdoProjectRoot {
    param([string]$StartPath = (Get-Location).Path)

    if ($env:CDO_PROJECT_ROOT -and (Test-Path -LiteralPath (Join-Path $env:CDO_PROJECT_ROOT 'project.cdo.json'))) {
        return (Resolve-CdoPath -Path $env:CDO_PROJECT_ROOT)
    }

    $resolved = Resolve-CdoPath -Path $StartPath
    if (Test-Path -LiteralPath $resolved -PathType Leaf) {
        $resolved = Split-Path -Parent $resolved
    }

    $dir = New-Object System.IO.DirectoryInfo($resolved)
    while ($null -ne $dir) {
        if (Test-Path -LiteralPath (Join-Path $dir.FullName 'project.cdo.json')) {
            return $dir.FullName
        }
        $dir = $dir.Parent
    }

    throw "No project.cdo.json found. Run 'cdo init .' first or pass --project <path>."
}

function Get-CdoRelativePath {
    param(
        [string]$From,
        [string]$To
    )

    $fromFull = [System.IO.Path]::GetFullPath($From).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $toFull = [System.IO.Path]::GetFullPath($To)
    $fromUri = New-Object System.Uri($fromFull)
    $toUri = New-Object System.Uri($toFull)
    $relative = [System.Uri]::UnescapeDataString($fromUri.MakeRelativeUri($toUri).ToString())
    return $relative.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
}

function ConvertTo-CdoForwardSlashPath {
    param([string]$Path)
    if ($null -eq $Path) { return $null }
    return $Path.Replace('\', '/')
}

function Get-CdoSafeName {
    param([string]$Name)
    $safe = $Name.ToLowerInvariant()
    $safe = [regex]::Replace($safe, '[^a-z0-9_]+', '_').Trim('_')
    if ([string]::IsNullOrWhiteSpace($safe)) { $safe = 'c_project' }
    if ($safe -match '^[0-9]') { $safe = "c_$safe" }
    return $safe
}

function Get-CdoVariableName {
    param([string]$Name)
    $safe = [regex]::Replace($Name.ToUpperInvariant(), '[^A-Z0-9]+', '_').Trim('_')
    if ([string]::IsNullOrWhiteSpace($safe)) { $safe = 'DEP' }
    if ($safe -match '^[0-9]') { $safe = "D_$safe" }
    return $safe
}

function Resolve-CdoTargets {
    param([object]$Config)

    # Check for new 'targets' array first
    $targetsArray = Get-CdoProperty -Object $Config -Name 'targets'
    if ($null -ne $targetsArray -and $targetsArray.Count -gt 0) {
        return @($targetsArray | ForEach-Object {
            $name = Get-CdoProperty -Object $_ -Name 'name'
            $type = Get-CdoProperty -Object $_ -Name 'type'
            $target = Get-CdoProperty -Object $_ -Name 'target' -Default $name
            [pscustomobject]@{
                name = $name
                type = $type
                target = $target
                sources = @(Get-CdoProperty -Object $_ -Name 'sources' -Default @())
                workingDirectory = Get-CdoProperty -Object $_ -Name 'workingDirectory' -Default '.'
                args = @(Get-CdoProperty -Object $_ -Name 'args' -Default @())
            }
        })
    }

    # Fall back to legacy format
    $result = @()
    $apps = @(Get-CdoProperty -Object $Config -Name 'apps' -Default @())
    foreach ($app in $apps) {
        $name = Get-CdoProperty -Object $app -Name 'name'
        $type = Get-CdoProperty -Object $app -Name 'type' -Default 'executable'
        $target = Get-CdoProperty -Object $app -Name 'target' -Default $name
        $result += [pscustomobject]@{
            name = $name
            type = $type
            target = $target
            sources = @(Get-CdoProperty -Object $app -Name 'sources' -Default @())
            workingDirectory = Get-CdoProperty -Object $app -Name 'workingDirectory' -Default '.'
            args = @(Get-CdoProperty -Object $app -Name 'args' -Default @())
        }
    }

    $tests = Get-CdoProperty -Object $Config -Name 'tests'
    if ($null -ne $tests) {
        $enabled = Get-CdoProperty -Object $tests -Name 'enabled' -Default $true
        if ($enabled) {
            $testTarget = Get-CdoProperty -Object $tests -Name 'target' -Default 'app_tests'
            $result += [pscustomobject]@{
                name = $testTarget
                type = 'test'
                target = $testTarget
                sources = @()
                workingDirectory = '.'
                args = @()
            }
        }
    }

    return @($result)
}

function Resolve-CdoDefaultTarget {
    param(
        [object[]]$Targets,
        [string[]]$AllowedTypes,
        [string]$Command
    )

    $matching = @($Targets | Where-Object {
        $type = Get-CdoProperty -Object $_ -Name 'type'
        $AllowedTypes -contains $type
    })

    if ($matching.Count -eq 0) {
        $typeList = $AllowedTypes -join ', '
        throw "No $typeList targets declared in project.cdo.json."
    }

    if ($matching.Count -eq 1) {
        return $matching[0]
    }

    $names = @($matching | ForEach-Object { Get-CdoProperty -Object $_ -Name 'name' })
    $nameList = $names -join ', '
    throw "Multiple targets exist for '$Command'. Specify one: $nameList"
}

function Get-CdoTargetByName {
    param(
        [object[]]$Targets,
        [string]$Name
    )

    $found = $Targets | Where-Object {
        $tName = Get-CdoProperty -Object $_ -Name 'name'
        $tTarget = Get-CdoProperty -Object $_ -Name 'target'
        ($tName -ieq $Name) -or ($tTarget -ieq $Name)
    }

    if ($null -ne $found) {
        if ($found -is [array]) { return $found[0] }
        return $found
    }

    $names = @($Targets | ForEach-Object { Get-CdoProperty -Object $_ -Name 'name' })
    $nameList = $names -join ', '
    throw "Target '$Name' not found. Available targets: $nameList"
}
