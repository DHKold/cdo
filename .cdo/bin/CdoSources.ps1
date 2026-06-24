<#
Source file management for cdo projects.
#>

$CdoSourceExtensions = @('.c', '.cpp', '.cc', '.cxx')

function Show-CdoSourceHelp {
    Write-Host "Source commands:"
    Write-Host "  cdo source list [--target <cmake-target>] [--pattern <glob>]"
    Write-Host "  cdo source include <file...> [--target <cmake-target>] [--pattern <glob>] [--all] [--dry-run]"
    Write-Host "  cdo source exclude <file...> [--target <cmake-target>] [--pattern <glob>] [--all] [--dry-run]"
    Write-Host "  cdo source cleanup [--target <cmake-target>] [--dry-run]"
    Write-Host "  cdo source sync [--target <cmake-target>] [--pattern <glob>] [--dry-run]"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  cdo source list"
    Write-Host "  cdo source include src/feature.c"
    Write-Host "  cdo source include --pattern src/features/*.c"
    Write-Host "  cdo source exclude --pattern src/experimental/*.c"
    Write-Host "  cdo source cleanup"
    Write-Host "  cdo source sync"
}

function Get-CdoCMakeListsPath {
    param([string]$Root)

    $path = Join-Path $Root 'CMakeLists.txt'
    if (-not (Test-Path -LiteralPath $path)) {
        throw "CMakeLists.txt was not found at $Root."
    }
    return $path
}

function Normalize-CdoProjectRelativePath {
    param(
        [string]$Root,
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Source path cannot be empty."
    }

    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if ([System.IO.Path]::IsPathRooted($Path)) {
        $full = [System.IO.Path]::GetFullPath($Path)
    } else {
        $full = [System.IO.Path]::GetFullPath((Join-Path $Root $Path))
    }

    $rootWithSlash = $rootFull + [System.IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($rootWithSlash, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Source path '$Path' is outside the project root."
    }

    $relative = Get-CdoRelativePath -From $rootFull -To $full
    $relative = ConvertTo-CdoForwardSlashPath -Path $relative
    while ($relative.StartsWith('./')) { $relative = $relative.Substring(2) }
    return $relative
}

function Get-CdoSourcePathFromCMakeLine {
    param([string]$Line)

    if ($null -eq $Line) { return $null }
    $candidate = ($Line -split '#', 2)[0].Trim()
    if ([string]::IsNullOrWhiteSpace($candidate)) { return $null }
    $candidate = $candidate.Trim('"')
    $candidate = $candidate.Trim("'")
    if ($candidate -notmatch '(?i)\.(c|cpp|cc|cxx)$') { return $null }
    if ($candidate -match '^\$|\(|\)') { return $null }
    return (ConvertTo-CdoForwardSlashPath -Path $candidate)
}

function Get-CdoCMakeSourceBlocks {
    param([string]$Content)

    $blocks = @()
    $pattern = '(?ms)add_(?<kind>library|executable)\s*\(\s*(?<target>[A-Za-z0-9_.:+-]+)(?<body>.*?)(?<close>^\s*\))'
    foreach ($match in [regex]::Matches($Content, $pattern)) {
        $sources = @()
        foreach ($line in ($match.Value -split "`r?`n")) {
            $source = Get-CdoSourcePathFromCMakeLine -Line $line
            if ($source) { $sources += $source }
        }

        $blocks += [pscustomobject]@{
            Kind = $match.Groups['kind'].Value
            Target = $match.Groups['target'].Value
            Index = $match.Index
            Length = $match.Length
            Text = $match.Value
            Sources = @($sources)
        }
    }

    return @($blocks)
}

function Get-CdoSourceBlocksForTarget {
    param(
        [object[]]$Blocks,
        [string]$Target
    )

    if ([string]::IsNullOrWhiteSpace($Target)) { return @() }
    return @($Blocks | Where-Object { $_.Target -ieq $Target })
}

function Resolve-CdoSourceDefaultTarget {
    param([object[]]$Blocks)

    $libraryBlocks = @($Blocks | Where-Object { $_.Kind -eq 'library' -and @($_.Sources | Where-Object {
        $ext = [System.IO.Path]::GetExtension($_).ToLowerInvariant()
        $_ -like 'src/*' -and $CdoSourceExtensions -contains $ext
    }).Count -gt 0 })
    if ($libraryBlocks.Count -gt 0) { return $libraryBlocks[0].Target }

    $sourceBlocks = @($Blocks | Where-Object { @($_.Sources | Where-Object {
        $ext = [System.IO.Path]::GetExtension($_).ToLowerInvariant()
        $_ -like 'src/*' -and $CdoSourceExtensions -contains $ext
    }).Count -gt 0 })
    if ($sourceBlocks.Count -gt 0) { return $sourceBlocks[0].Target }

    throw "No CMake target with src/ C/C++ sources was found. Pass --target <cmake-target>."
}

function Get-CdoDiscoveredSources {
    param(
        [string]$Root,
        [string]$Pattern = ''
    )

    $srcRoot = Join-Path $Root 'src'
    if (-not (Test-Path -LiteralPath $srcRoot)) { return @() }

    $sources = @()
    foreach ($file in (Get-ChildItem -LiteralPath $srcRoot -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $CdoSourceExtensions -contains $_.Extension.ToLowerInvariant() })) {
        $relative = Normalize-CdoProjectRelativePath -Root $Root -Path $file.FullName
        if (-not [string]::IsNullOrWhiteSpace($Pattern)) {
            $pattern = ConvertTo-CdoForwardSlashPath -Path $Pattern
            if (($relative -notlike $pattern) -and ($file.Name -notlike $pattern)) { continue }
        }
        $sources += $relative
    }

    return @($sources | Sort-Object -Unique)
}

function Get-CdoIncludedSourceMap {
    param([object[]]$Blocks)

    $map = @{}
    foreach ($block in $Blocks) {
        foreach ($source in $block.Sources) {
            if (-not $map.ContainsKey($source)) { $map[$source] = @() }
            $map[$source] = @($map[$source]) + $block.Target
        }
    }
    return $map
}

function Resolve-CdoSourceSelection {
    param(
        [string]$Root,
        [string[]]$Files,
        [string]$Pattern,
        [switch]$All,
        [bool]$RequireExists = $true
    )

    if ($Files.Count -gt 0) {
        $result = @()
        foreach ($file in $Files) {
            $relative = Normalize-CdoProjectRelativePath -Root $Root -Path $file
            if ($relative -notmatch '(?i)\.(c|cpp|cc|cxx)$') {
                throw "Only C/C++ source files (.c, .cpp, .cc, .cxx) can be managed by this command: $relative"
            }
            $full = Join-Path $Root ($relative -replace '/', '\')
            if ($RequireExists -and -not (Test-Path -LiteralPath $full)) {
                throw "Source file does not exist: $relative"
            }
            $result += $relative
        }
        return @($result | Sort-Object -Unique)
    }

    if ($All -or -not [string]::IsNullOrWhiteSpace($Pattern)) {
        return @(Get-CdoDiscoveredSources -Root $Root -Pattern $Pattern)
    }

    throw "No sources selected. Pass files, --pattern <glob>, or --all."
}

function Convert-CdoSourceBlockToText {
    param(
        [object]$Block,
        [string[]]$Sources
    )

    $lines = @($Block.Text -split "`r?`n")
    $kept = New-Object 'System.Collections.Generic.List[string]'

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        $isClosing = ($i -eq ($lines.Count - 1))
        if ($isClosing) { continue }

        $source = Get-CdoSourcePathFromCMakeLine -Line $line
        if ($source) { continue }
        $kept.Add($line)
    }

    foreach ($source in $Sources) {
        $kept.Add("    $source")
    }
    $kept.Add(')')

    return ($kept.ToArray() -join "`r`n")
}

function Set-CdoTargetSourcesInCMake {
    param(
        [string]$CMakePath,
        [object[]]$Blocks,
        [hashtable]$TargetSources,
        [switch]$DryRun
    )

    $content = Get-Content -LiteralPath $CMakePath -Raw
    $targets = @($TargetSources.Keys)
    $targets = @($targets | Sort-Object)
    foreach ($target in $targets) {
        $matches = @(Get-CdoSourceBlocksForTarget -Blocks $Blocks -Target $target)
        if ($matches.Count -eq 0) {
            throw "CMake target '$target' was not found."
        }
        if ($matches.Count -gt 1) {
            throw "CMake target '$target' appears more than once; source editing is ambiguous."
        }
    }

    $orderedBlocks = @($Blocks | Where-Object { $TargetSources.ContainsKey($_.Target) } | Sort-Object Index -Descending)
    foreach ($block in $orderedBlocks) {
        $newText = Convert-CdoSourceBlockToText -Block $block -Sources @($TargetSources[$block.Target])
        $content = $content.Remove($block.Index, $block.Length).Insert($block.Index, $newText)
    }

    if ($DryRun) {
        Write-CdoLog -Level INFO -Message "dry-run: would update $CMakePath"
        return
    }

    Write-CdoTextFile -Path $CMakePath -Content $content
    Write-CdoLog -Level OK -Message "updated $CMakePath"
}

function Show-CdoSources {
    param(
        [string]$Root,
        [string]$Target = '',
        [string]$Pattern = ''
    )

    $cmake = Get-CdoCMakeListsPath -Root $Root
    $content = Get-Content -LiteralPath $cmake -Raw
    $blocks = Get-CdoCMakeSourceBlocks -Content $content
    if ($blocks.Count -eq 0) { throw "No editable add_library/add_executable source blocks were found in CMakeLists.txt." }

    if (-not [string]::IsNullOrWhiteSpace($Target)) {
        $targetBlocks = @(Get-CdoSourceBlocksForTarget -Blocks $blocks -Target $Target)
        if ($targetBlocks.Count -eq 0) { throw "CMake target '$Target' was not found." }
        $blocks = $targetBlocks
    }

    $included = Get-CdoIncludedSourceMap -Blocks $blocks
    $discovered = @(Get-CdoDiscoveredSources -Root $Root -Pattern $Pattern)
    $all = @($discovered + @($included.Keys) | Sort-Object -Unique)

    Write-Host ("{0,-10} {1,-24} {2}" -f 'status', 'target', 'path') -ForegroundColor Cyan
    Write-Host ("{0,-10} {1,-24} {2}" -f '------', '------', '----') -ForegroundColor DarkGray
    foreach ($source in $all) {
        $full = Join-Path $Root ($source -replace '/', '\')
        if ($included.ContainsKey($source)) {
            $status = if (Test-Path -LiteralPath $full) { 'included' } else { 'missing' }
            $targets = (@($included[$source]) -join ',')
        } else {
            $status = 'excluded'
            $targets = '-'
        }
        $color = switch ($status) {
            'included' { 'Green' }
            'excluded' { 'Yellow' }
            'missing' { 'Red' }
            default { 'White' }
        }
        Write-Host ("{0,-10} {1,-24} {2}" -f $status, $targets, $source) -ForegroundColor $color
    }
}

function Add-CdoSources {
    param(
        [string]$Root,
        [string[]]$Files,
        [string]$Target = '',
        [string]$Pattern = '',
        [switch]$All,
        [switch]$DryRun
    )

    $cmake = Get-CdoCMakeListsPath -Root $Root
    $content = Get-Content -LiteralPath $cmake -Raw
    $blocks = Get-CdoCMakeSourceBlocks -Content $content
    if ($blocks.Count -eq 0) { throw "No editable add_library/add_executable source blocks were found in CMakeLists.txt." }

    if ([string]::IsNullOrWhiteSpace($Target)) {
        $Target = Resolve-CdoSourceDefaultTarget -Blocks $blocks
        Write-CdoLog -Level INFO -Message "using default source target: $Target"
    }

    $targetBlocks = @(Get-CdoSourceBlocksForTarget -Blocks $blocks -Target $Target)
    if ($targetBlocks.Count -eq 0) { throw "CMake target '$Target' was not found." }
    if ($targetBlocks.Count -gt 1) { throw "CMake target '$Target' appears more than once; source editing is ambiguous." }

    $selection = @(Resolve-CdoSourceSelection -Root $Root -Files $Files -Pattern $Pattern -All:$All)
    $globalIncluded = Get-CdoIncludedSourceMap -Blocks $blocks
    $targetSet = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($source in $targetBlocks[0].Sources) { [void]$targetSet.Add($source) }

    $newSources = @($targetBlocks[0].Sources)
    $added = 0
    foreach ($source in $selection) {
        if ($targetSet.Contains($source)) { continue }
        if ($Files.Count -eq 0 -and $globalIncluded.ContainsKey($source)) { continue }
        $newSources += $source
        [void]$targetSet.Add($source)
        $added++
    }

    $updates = @{}
    $updates[$Target] = @($newSources)
    Set-CdoTargetSourcesInCMake -CMakePath $cmake -Blocks $blocks -TargetSources $updates -DryRun:$DryRun
    Write-CdoLog -Level OK -Message "included $added source(s) in $Target"
}

function Remove-CdoSources {
    param(
        [string]$Root,
        [string[]]$Files,
        [string]$Target = '',
        [string]$Pattern = '',
        [switch]$All,
        [switch]$DryRun
    )

    $cmake = Get-CdoCMakeListsPath -Root $Root
    $content = Get-Content -LiteralPath $cmake -Raw
    $blocks = Get-CdoCMakeSourceBlocks -Content $content
    if ($blocks.Count -eq 0) { throw "No editable add_library/add_executable source blocks were found in CMakeLists.txt." }

    $selection = @(Resolve-CdoSourceSelection -Root $Root -Files $Files -Pattern $Pattern -All:$All -RequireExists:$false)
    $removeSet = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($source in $selection) { [void]$removeSet.Add($source) }

    $editableBlocks = $blocks
    if (-not [string]::IsNullOrWhiteSpace($Target)) {
        $editableBlocks = @(Get-CdoSourceBlocksForTarget -Blocks $blocks -Target $Target)
        if ($editableBlocks.Count -eq 0) { throw "CMake target '$Target' was not found." }
    }

    $updates = @{}
    $removed = 0
    foreach ($block in $editableBlocks) {
        $remaining = @()
        foreach ($source in $block.Sources) {
            if ($removeSet.Contains($source)) {
                $removed++
            } else {
                $remaining += $source
            }
        }
        if ($remaining.Count -ne $block.Sources.Count) {
            $updates[$block.Target] = @($remaining)
        }
    }

    if ($updates.Count -eq 0) {
        Write-CdoLog -Level INFO -Message "no matching sources were included"
        return
    }

    Set-CdoTargetSourcesInCMake -CMakePath $cmake -Blocks $blocks -TargetSources $updates -DryRun:$DryRun
    Write-CdoLog -Level OK -Message "excluded $removed source(s)"
}

function Remove-CdoMissingSources {
    param(
        [string]$Root,
        [string]$Target = '',
        [switch]$DryRun
    )

    $cmake = Get-CdoCMakeListsPath -Root $Root
    $content = Get-Content -LiteralPath $cmake -Raw
    $blocks = Get-CdoCMakeSourceBlocks -Content $content
    if ($blocks.Count -eq 0) { throw "No editable add_library/add_executable source blocks were found in CMakeLists.txt." }

    $editableBlocks = $blocks
    if (-not [string]::IsNullOrWhiteSpace($Target)) {
        $editableBlocks = @(Get-CdoSourceBlocksForTarget -Blocks $blocks -Target $Target)
        if ($editableBlocks.Count -eq 0) { throw "CMake target '$Target' was not found." }
    }

    $updates = @{}
    $removed = 0
    foreach ($block in $editableBlocks) {
        $remaining = @()
        foreach ($source in $block.Sources) {
            $full = Join-Path $Root ($source -replace '/', '\')
            if (Test-Path -LiteralPath $full) {
                $remaining += $source
            } else {
                $removed++
                Write-CdoLog -Level INFO -Message "removing missing source reference: $source"
            }
        }
        if ($remaining.Count -ne $block.Sources.Count) {
            $updates[$block.Target] = @($remaining)
        }
    }

    if ($updates.Count -eq 0) {
        Write-CdoLog -Level OK -Message "no missing source references"
        return 0
    }

    Set-CdoTargetSourcesInCMake -CMakePath $cmake -Blocks $blocks -TargetSources $updates -DryRun:$DryRun
    Write-CdoLog -Level OK -Message "removed $removed missing source reference(s)"
    return $removed
}

function Sync-CdoSources {
    param(
        [string]$Root,
        [string]$Target = '',
        [string]$Pattern = '',
        [switch]$DryRun
    )

    Remove-CdoMissingSources -Root $Root -Target $Target -DryRun:$DryRun | Out-Null
    Add-CdoSources -Root $Root -Files @() -Target $Target -Pattern $Pattern -All -DryRun:$DryRun
}
