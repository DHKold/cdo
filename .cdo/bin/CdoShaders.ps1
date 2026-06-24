<#
Shader workflow helpers for SDL3 GPU projects.
#>

function Get-CdoShaderStages {
    return @('vertex', 'fragment', 'compute')
}

function Resolve-CdoShaderStage {
    param([string]$Stage)

    $key = $Stage.ToLowerInvariant()
    switch ($key) {
        'vert' { return 'vertex' }
        'vertex' { return 'vertex' }
        'frag' { return 'fragment' }
        'fragment' { return 'fragment' }
        'pixel' { return 'fragment' }
        'ps' { return 'fragment' }
        'comp' { return 'compute' }
        'compute' { return 'compute' }
        'cs' { return 'compute' }
        default {
            $valid = (Get-CdoShaderStages) -join ', '
            throw "Unknown shader stage '$Stage'. Expected one of: $valid"
        }
    }
}

function Get-CdoShaderExtension {
    param([string]$Stage)

    switch (Resolve-CdoShaderStage -Stage $Stage) {
        'vertex' { return 'vert' }
        'fragment' { return 'frag' }
        'compute' { return 'comp' }
    }
}

function Get-CdoShaderProfile {
    param([string]$Stage)

    switch (Resolve-CdoShaderStage -Stage $Stage) {
        'vertex' { return 'vs_6_0' }
        'fragment' { return 'ps_6_0' }
        'compute' { return 'cs_6_0' }
    }
}

function Get-CdoGlslangStage {
    param([string]$Stage)

    switch (Resolve-CdoShaderStage -Stage $Stage) {
        'vertex' { return 'vert' }
        'fragment' { return 'frag' }
        'compute' { return 'comp' }
    }
}

function Test-CdoShaderProject {
    param([object]$Config)

    $toolchain = Get-CdoProperty -Object $Config -Name 'shaderToolchain'
    $entries = @(Get-CdoProperty -Object $Config -Name 'shaders' -Default @())
    return ($null -ne $toolchain -and $entries.Count -gt 0)
}

function Get-CdoShaderTemplate {
    param([string]$Stage)

    switch (Resolve-CdoShaderStage -Stage $Stage) {
        'vertex' {
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
        'fragment' {
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
        'compute' {
            return @'
RWByteAddressBuffer OutputBuffer : register(u0, space0);

[numthreads(8, 8, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    uint index = dispatch_id.y * 8 + dispatch_id.x;
    OutputBuffer.Store(index * 4, index);
}
'@
        }
    }
}

function Get-CdoShaderConfig {
    param([object]$Config)

    $toolchain = Get-CdoProperty -Object $Config -Name 'shaderToolchain'
    if ($null -eq $toolchain) {
        $toolchain = [ordered]@{
            sourceDir = 'assets/shaders/src'
            outputDir = 'assets/shaders/compiled'
            language = 'hlsl'
            entrypoint = 'main'
            targets = @('dxil', 'spirv')
        }
        Set-CdoProperty -Object $Config -Name 'shaderToolchain' -Value $toolchain
    }

    if ($null -eq (Get-CdoProperty -Object $Config -Name 'shaders')) {
        Set-CdoProperty -Object $Config -Name 'shaders' -Value @()
    }

    return $toolchain
}

function Get-CdoShaderEntries {
    param([object]$Config)
    return @(Get-CdoProperty -Object $Config -Name 'shaders' -Default @())
}

function Set-CdoShaderEntries {
    param([object]$Config, [object[]]$Entries)
    Set-CdoProperty -Object $Config -Name 'shaders' -Value @($Entries)
}

function Get-CdoShaderStageFromPath {
    param([string]$Path)

    $leaf = Split-Path -Leaf $Path
    if ($leaf -match '(?i)\.vert\.hlsl$|\.vertex\.hlsl$') { return 'vertex' }
    if ($leaf -match '(?i)\.frag\.hlsl$|\.fragment\.hlsl$|\.pixel\.hlsl$') { return 'fragment' }
    if ($leaf -match '(?i)\.comp\.hlsl$|\.compute\.hlsl$') { return 'compute' }
    return ''
}

function Get-CdoShaderNameFromPath {
    param([string]$Path)

    $leaf = Split-Path -Leaf $Path
    return ([regex]::Replace($leaf, '(?i)\.(vert|vertex|frag|fragment|pixel|comp|compute)\.hlsl$', ''))
}

function New-CdoShaderEntryFromSource {
    param(
        [object]$Toolchain,
        [string]$Source
    )

    $stage = Get-CdoShaderStageFromPath -Path $Source
    if ([string]::IsNullOrWhiteSpace($stage)) {
        throw "Cannot infer shader stage from '$Source'. Use .vert.hlsl, .frag.hlsl, or .comp.hlsl."
    }

    return [ordered]@{
        name = Get-CdoShaderNameFromPath -Path $Source
        stage = $stage
        source = $Source
        entrypoint = [string](Get-CdoProperty -Object $Toolchain -Name 'entrypoint' -Default 'main')
    }
}

function Get-CdoDiscoveredShaderSources {
    param(
        [string]$Root,
        [object]$Toolchain,
        [string]$Pattern = ''
    )

    $sourceDir = [string](Get-CdoProperty -Object $Toolchain -Name 'sourceDir' -Default 'assets/shaders/src')
    $absSourceDir = Join-Path $Root ($sourceDir -replace '/', '\')
    if (-not (Test-Path -LiteralPath $absSourceDir)) { return @() }

    $result = @()
    foreach ($file in (Get-ChildItem -LiteralPath $absSourceDir -Recurse -File -Filter '*.hlsl' -ErrorAction SilentlyContinue)) {
        $relative = ConvertTo-CdoForwardSlashPath -Path (Get-CdoRelativePath -From $Root -To $file.FullName)
        if ([string]::IsNullOrWhiteSpace((Get-CdoShaderStageFromPath -Path $relative))) { continue }
        if (-not [string]::IsNullOrWhiteSpace($Pattern)) {
            $pattern = ConvertTo-CdoForwardSlashPath -Path $Pattern
            if (($relative -notlike $pattern) -and ($file.Name -notlike $pattern)) { continue }
        }
        $result += $relative
    }

    return @($result | Sort-Object -Unique)
}

function Get-CdoShaderEntrySourceMap {
    param([object[]]$Entries)

    $map = @{}
    foreach ($entry in $Entries) {
        $source = ConvertTo-CdoForwardSlashPath -Path ([string](Get-CdoProperty -Object $entry -Name 'source' -Default ''))
        if ([string]::IsNullOrWhiteSpace($source)) { continue }
        $map[$source] = $entry
    }
    return $map
}

function Resolve-CdoShaderSourceSelection {
    param(
        [string]$Root,
        [object]$Toolchain,
        [object[]]$Entries,
        [string[]]$Files,
        [string]$Pattern,
        [switch]$All,
        [switch]$RegisteredOnly,
        [switch]$RequireExists
    )

    if ($Files.Count -gt 0) {
        $result = @()
        foreach ($file in $Files) {
            $relative = Normalize-CdoProjectRelativePath -Root $Root -Path $file
            if ($relative -notmatch '(?i)\.hlsl$') {
                throw "Only .hlsl shader source files can be managed by this command: $relative"
            }
            if ([string]::IsNullOrWhiteSpace((Get-CdoShaderStageFromPath -Path $relative))) {
                throw "Cannot infer shader stage from '$relative'. Use .vert.hlsl, .frag.hlsl, or .comp.hlsl."
            }
            $full = Join-Path $Root ($relative -replace '/', '\')
            if ($RequireExists -and -not (Test-Path -LiteralPath $full)) {
                throw "Shader source file does not exist: $relative"
            }
            $result += $relative
        }
        return @($result | Sort-Object -Unique)
    }

    if ($RegisteredOnly) {
        $registered = @()
        foreach ($entry in $Entries) {
            $source = ConvertTo-CdoForwardSlashPath -Path ([string](Get-CdoProperty -Object $entry -Name 'source' -Default ''))
            if ([string]::IsNullOrWhiteSpace($source)) { continue }
            if (-not [string]::IsNullOrWhiteSpace($Pattern)) {
                $pattern = ConvertTo-CdoForwardSlashPath -Path $Pattern
                $leaf = Split-Path -Leaf $source
                if (($source -notlike $pattern) -and ($leaf -notlike $pattern)) { continue }
            }
            $registered += $source
        }
        if ($All -or -not [string]::IsNullOrWhiteSpace($Pattern)) {
            return @($registered | Sort-Object -Unique)
        }
    }

    if ($All -or -not [string]::IsNullOrWhiteSpace($Pattern)) {
        return @(Get-CdoDiscoveredShaderSources -Root $Root -Toolchain $Toolchain -Pattern $Pattern)
    }

    throw "No shaders selected. Pass files, --pattern <glob>, or --all."
}

function Add-CdoShader {
    param(
        [string]$Root,
        [string]$Stage,
        [string]$Name,
        [switch]$Force
    )

    $config = Load-Config -Root $Root
    $toolchain = Get-CdoShaderConfig -Config $config
    $stageName = Resolve-CdoShaderStage -Stage $Stage
    $extension = Get-CdoShaderExtension -Stage $stageName
    $sourceDir = [string](Get-CdoProperty -Object $toolchain -Name 'sourceDir' -Default 'assets/shaders/src')
    $relativePath = "$sourceDir/$Name.$extension.hlsl"
    $path = Join-Path $Root ($relativePath.Replace('/', '\'))

    if ((Test-Path -LiteralPath $path) -and -not $Force) {
        Write-CdoLog -Level INFO -Message "kept existing $relativePath"
    } else {
        Write-CdoTextFile -Path $path -Content (Get-CdoShaderTemplate -Stage $stageName)
        Write-CdoLog -Level OK -Message "wrote $relativePath"
    }

    $entries = @(Get-CdoShaderEntries -Config $config)
    $entries = @($entries | Where-Object {
        -not ((Get-CdoProperty -Object $_ -Name 'name') -eq $Name -and (Get-CdoProperty -Object $_ -Name 'stage') -eq $stageName)
    })
    $entries += [ordered]@{
        name = $Name
        stage = $stageName
        source = $relativePath
        entrypoint = [string](Get-CdoProperty -Object $toolchain -Name 'entrypoint' -Default 'main')
    }
    Set-CdoShaderEntries -Config $config -Entries $entries
    Save-Config -Config $config -Root $Root
    Write-CdoLog -Level OK -Message "registered $stageName shader '$Name'"
}

function Show-CdoShaders {
    param([string]$Root)

    $config = Load-Config -Root $Root
    $toolchain = Get-CdoShaderConfig -Config $config
    $entries = @(Get-CdoShaderEntries -Config $config)
    $entryMap = Get-CdoShaderEntrySourceMap -Entries $entries
    $discovered = @(Get-CdoDiscoveredShaderSources -Root $Root -Toolchain $toolchain)
    $all = @($discovered + @($entryMap.Keys) | Sort-Object -Unique)

    Write-Host "Shader source: $((Get-CdoProperty -Object $toolchain -Name 'sourceDir' -Default 'assets/shaders/src'))"
    Write-Host "Shader output: $((Get-CdoProperty -Object $toolchain -Name 'outputDir' -Default 'assets/shaders/compiled'))"
    Write-Host "Targets: $(@(Get-CdoProperty -Object $toolchain -Name 'targets' -Default @()) -join ', ')"
    Write-Host ""
    Write-Host "Shaders:"
    if ($all.Count -eq 0) {
        Write-Host "  (none)"
        return
    }

    Write-Host ("{0,-10} {1,-12} {2,-8} {3}" -f 'status', 'name', 'stage', 'source') -ForegroundColor Cyan
    Write-Host ("{0,-10} {1,-12} {2,-8} {3}" -f '------', '----', '-----', '------') -ForegroundColor DarkGray
    foreach ($source in $all) {
        $full = Join-Path $Root ($source -replace '/', '\')
        if ($entryMap.ContainsKey($source)) {
            $entry = $entryMap[$source]
            $status = if (Test-Path -LiteralPath $full) { 'included' } else { 'missing' }
            $name = [string](Get-CdoProperty -Object $entry -Name 'name' -Default (Get-CdoShaderNameFromPath -Path $source))
            $stage = [string](Get-CdoProperty -Object $entry -Name 'stage' -Default (Get-CdoShaderStageFromPath -Path $source))
        } else {
            $status = 'excluded'
            $name = Get-CdoShaderNameFromPath -Path $source
            $stage = Get-CdoShaderStageFromPath -Path $source
        }
        $color = switch ($status) {
            'included' { 'Green' }
            'excluded' { 'Yellow' }
            'missing' { 'Red' }
            default { 'White' }
        }
        Write-Host ("{0,-10} {1,-12} {2,-8} {3}" -f $status, $name, $stage, $source) -ForegroundColor $color
    }
}

function Find-CdoShaderTool {
    param([string]$Root, [string]$Name)
    if (Get-Command -Name 'Find-CdoToolCommand' -CommandType Function -ErrorAction SilentlyContinue) {
        $tool = Find-CdoToolCommand -Root $Root -Command $Name
        if ($tool) { return $tool }
    }
    return Get-Command $Name -ErrorAction SilentlyContinue
}

function Get-CdoShaderToolPath {
    param(
        [string]$Root,
        [string[]]$Names
    )

    foreach ($name in $Names) {
        $tool = Find-CdoShaderTool -Root $Root -Name $name
        if ($tool) { return [string]$tool }
    }

    return ''
}

function Invoke-CdoShaderDoctor {
    param([string]$Root)

    $config = Load-Config -Root $Root
    $toolchain = Get-CdoShaderConfig -Config $config
    $entries = @(Get-CdoShaderEntries -Config $config)

    Write-CdoLog -Level INFO -Message "shader source: $(Get-CdoProperty -Object $toolchain -Name 'sourceDir')"
    Write-CdoLog -Level INFO -Message "shader output: $(Get-CdoProperty -Object $toolchain -Name 'outputDir')"

    foreach ($tool in @('SDL_shadercross', 'shadercross', 'dxc', 'glslangValidator', 'spirv-val', 'spirv-opt', 'renderdoccmd')) {
        if (Find-CdoShaderTool -Root $Root -Name $tool) {
            Write-CdoLog -Level OK -Message "$tool found"
        } else {
            Write-CdoLog -Level WARN -Message "$tool missing"
        }
    }

    foreach ($entry in $entries) {
        $source = Join-Path $Root ([string](Get-CdoProperty -Object $entry -Name 'source')).Replace('/', '\')
        if (Test-Path -LiteralPath $source) {
            Write-CdoLog -Level OK -Message "shader source found: $(Get-CdoProperty -Object $entry -Name 'source')"
        } else {
            Write-CdoLog -Level WARN -Message "shader source missing: $(Get-CdoProperty -Object $entry -Name 'source')"
        }
    }
}

function Add-CdoShaderSources {
    param(
        [string]$Root,
        [string[]]$Files,
        [string]$Pattern = '',
        [switch]$All,
        [switch]$DryRun
    )

    $config = Load-Config -Root $Root
    $toolchain = Get-CdoShaderConfig -Config $config
    $entries = @(Get-CdoShaderEntries -Config $config)
    $selection = @(Resolve-CdoShaderSourceSelection -Root $Root -Toolchain $toolchain -Entries $entries -Files $Files -Pattern $Pattern -All:$All -RequireExists)
    $entryMap = Get-CdoShaderEntrySourceMap -Entries $entries

    $newEntries = @($entries)
    $added = 0
    foreach ($source in $selection) {
        if ($entryMap.ContainsKey($source)) { continue }
        $newEntries += (New-CdoShaderEntryFromSource -Toolchain $toolchain -Source $source)
        $entryMap[$source] = $true
        $added++
    }

    if ($DryRun) {
        Write-CdoLog -Level INFO -Message "dry-run: would register $added shader source(s)"
        return $added
    }

    Set-CdoShaderEntries -Config $config -Entries $newEntries
    Save-Config -Config $config -Root $Root
    Write-CdoLog -Level OK -Message "registered $added shader source(s)"
    return $added
}

function Remove-CdoShaderSources {
    param(
        [string]$Root,
        [string[]]$Files,
        [string]$Pattern = '',
        [switch]$All,
        [switch]$DryRun
    )

    $config = Load-Config -Root $Root
    $toolchain = Get-CdoShaderConfig -Config $config
    $entries = @(Get-CdoShaderEntries -Config $config)
    $selection = @(Resolve-CdoShaderSourceSelection -Root $Root -Toolchain $toolchain -Entries $entries -Files $Files -Pattern $Pattern -All:$All -RegisteredOnly)
    $removeSet = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($source in $selection) { [void]$removeSet.Add($source) }

    $remaining = @()
    $removed = 0
    foreach ($entry in $entries) {
        $source = ConvertTo-CdoForwardSlashPath -Path ([string](Get-CdoProperty -Object $entry -Name 'source' -Default ''))
        if ($removeSet.Contains($source)) {
            $removed++
        } else {
            $remaining += $entry
        }
    }

    if ($removed -eq 0) {
        Write-CdoLog -Level INFO -Message "no matching shaders were registered"
        return 0
    }

    if ($DryRun) {
        Write-CdoLog -Level INFO -Message "dry-run: would unregister $removed shader source(s)"
        return $removed
    }

    Set-CdoShaderEntries -Config $config -Entries $remaining
    Save-Config -Config $config -Root $Root
    Write-CdoLog -Level OK -Message "unregistered $removed shader source(s)"
    return $removed
}

function Remove-CdoMissingShaders {
    param(
        [string]$Root,
        [switch]$DryRun
    )

    $config = Load-Config -Root $Root
    Get-CdoShaderConfig -Config $config | Out-Null
    $entries = @(Get-CdoShaderEntries -Config $config)

    $remaining = @()
    $removed = 0
    foreach ($entry in $entries) {
        $source = ConvertTo-CdoForwardSlashPath -Path ([string](Get-CdoProperty -Object $entry -Name 'source' -Default ''))
        $full = Join-Path $Root ($source -replace '/', '\')
        if ([string]::IsNullOrWhiteSpace($source) -or -not (Test-Path -LiteralPath $full)) {
            $removed++
            Write-CdoLog -Level INFO -Message "removing missing shader reference: $source"
        } else {
            $remaining += $entry
        }
    }

    if ($removed -eq 0) {
        Write-CdoLog -Level OK -Message "no missing shader references"
        return 0
    }

    if ($DryRun) {
        Write-CdoLog -Level INFO -Message "dry-run: would unregister $removed missing shader reference(s)"
        return $removed
    }

    Set-CdoShaderEntries -Config $config -Entries $remaining
    Save-Config -Config $config -Root $Root
    Write-CdoLog -Level OK -Message "removed $removed missing shader reference(s)"
    return $removed
}

function Sync-CdoShaders {
    param(
        [string]$Root,
        [string]$Pattern = '',
        [switch]$DryRun
    )

    Remove-CdoMissingShaders -Root $Root -DryRun:$DryRun | Out-Null
    Add-CdoShaderSources -Root $Root -Files @() -Pattern $Pattern -All -DryRun:$DryRun | Out-Null
}

function Invoke-CdoShaderCompile {
    param(
        [string]$Root,
        [switch]$DryRun,
        [switch]$Strict
    )

    $config = Load-Config -Root $Root
    if (Get-Command -Name 'Add-CdoToolPathForProcess' -CommandType Function -ErrorAction SilentlyContinue) {
        Add-CdoToolPathForProcess -Root $Root
    }
    $toolchain = Get-CdoShaderConfig -Config $config
    $entries = @(Get-CdoShaderEntries -Config $config)
    $outputDir = [string](Get-CdoProperty -Object $toolchain -Name 'outputDir' -Default 'assets/shaders/compiled')
    $targets = @(Get-CdoProperty -Object $toolchain -Name 'targets' -Default @('dxil', 'spirv'))

    if ($entries.Count -eq 0) {
        Write-CdoLog -Level WARN -Message "no shaders registered. Use 'cdo shader add <stage> <name>'."
        return
    }

    foreach ($entry in $entries) {
        $stage = Resolve-CdoShaderStage -Stage (Get-CdoProperty -Object $entry -Name 'stage')
        $name = [string](Get-CdoProperty -Object $entry -Name 'name')
        $sourceRel = [string](Get-CdoProperty -Object $entry -Name 'source')
        $source = Join-Path $Root ($sourceRel.Replace('/', '\'))
        $entrypoint = [string](Get-CdoProperty -Object $entry -Name 'entrypoint' -Default 'main')
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Shader source not found: $sourceRel"
        }

        foreach ($target in $targets) {
            $targetName = [string]$target
            $targetDir = Join-Path $Root (Join-Path $outputDir $targetName)
            New-CdoDirectory -Path $targetDir
            $extension = switch ($targetName) {
                'dxil' { 'dxil' }
                'dxbc' { 'dxbc' }
                'spirv' { 'spv' }
                'msl' { 'msl' }
                default { $targetName }
            }
            $outFile = Join-Path $targetDir ("$name.$stage.$extension")

            if ($DryRun) {
                Write-Host "Would compile $sourceRel -> $(Get-CdoRelativePath -From $Root -To $outFile)"
                continue
            }

            if ($targetName -eq 'dxil' -and ($dxc = Find-CdoShaderTool -Root $Root -Name 'dxc')) {
                $profile = Get-CdoShaderProfile -Stage $stage
                $null = Invoke-CdoNative -FilePath $dxc -Arguments @('-T', $profile, '-E', $entrypoint, '-Fo', $outFile, $source) -WorkingDirectory $Root -ThrowOnError
                Write-CdoLog -Level OK -Message "compiled $(Get-CdoRelativePath -From $Root -To $outFile)"
            } elseif ($targetName -eq 'spirv' -and ($dxcSpv = Find-CdoShaderTool -Root $Root -Name 'dxc')) {
                $profile = Get-CdoShaderProfile -Stage $stage
                $null = Invoke-CdoNative -FilePath $dxcSpv -Arguments @('-spirv', '-T', $profile, '-E', $entrypoint, '-Fo', $outFile, $source) -WorkingDirectory $Root -ThrowOnError
                Write-CdoLog -Level OK -Message "compiled $(Get-CdoRelativePath -From $Root -To $outFile)"
            } elseif ($targetName -eq 'spirv' -and ($glslang = Find-CdoShaderTool -Root $Root -Name 'glslangValidator')) {
                $glStage = Get-CdoGlslangStage -Stage $stage
                $null = Invoke-CdoNative -FilePath $glslang -Arguments @('-D', '-V', '-e', $entrypoint, '-S', $glStage, '-o', $outFile, $source) -WorkingDirectory $Root -ThrowOnError
                Write-CdoLog -Level OK -Message "compiled $(Get-CdoRelativePath -From $Root -To $outFile)"
            } else {
                $message = "no compiler path for $targetName/$stage. Install SDL_shadercross, dxc, or glslangValidator, or run with --dry-run."
                if ($Strict -and $targetName -ne 'msl') { throw $message }
                Write-CdoLog -Level WARN -Message $message
            }
        }
    }
}

function Clear-CdoShaderOutputs {
    param([string]$Root)

    $config = Load-Config -Root $Root
    $toolchain = Get-CdoShaderConfig -Config $config
    $outputDir = Join-Path $Root ([string](Get-CdoProperty -Object $toolchain -Name 'outputDir' -Default 'assets/shaders/compiled')).Replace('/', '\')
    if (Test-Path -LiteralPath $outputDir) {
        Remove-Item -LiteralPath $outputDir -Recurse -Force
    }
    New-CdoDirectory -Path $outputDir
    Write-CdoLog -Level OK -Message "cleaned shader outputs"
}
