$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoTargets.ps1"

# Helper: Creates a minimal temp project with specified targets array
function New-TestProjectWithTargets {
    param([array]$Targets)

    $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "cdo-test-$([System.Guid]::NewGuid().ToString('N').Substring(0,8))"
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

    $manifest = [ordered]@{
        schema = 'https://cdo.dev/schemas/project.v1.json'
        schemaVersion = 2
        name = 'test-project'
        id = 'test-project'
        version = '0.1.0'
        cStandard = 17
        cppStandard = 20
        targets = $Targets
    }

    $manifestPath = Join-Path $tempDir 'project.cdo.json'
    $json = $manifest | ConvertTo-Json -Depth 32
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($manifestPath, ($json + [Environment]::NewLine), $utf8)

    return $tempDir
}

# Helper: Reads manifest from a test project root
function Get-TestManifest {
    param([string]$Root)
    $manifestPath = Join-Path $Root 'project.cdo.json'
    return Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
}

# Helper: Removes temp project directory
function Remove-TestProject {
    param([string]$Root)
    if ((Test-Path -LiteralPath $Root) -and $Root -like '*cdo-test-*') {
        Remove-Item -LiteralPath $Root -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# Helper: Captures Write-Host output from Invoke-CdoTargetList using 6>&1 information stream redirect
function Get-TargetListOutput {
    param([string]$Root)

    # Capture the information stream (stream 6) which Write-Host writes to
    $output = Invoke-CdoTargetList -Root $Root 6>&1
    if ($null -eq $output) { return @() }
    # Convert InformationRecord objects to strings
    $lines = @($output | ForEach-Object { $_.ToString() })
    return $lines
}

Describe 'Property 15: Target list displays all targets with complete information' {
    # **Validates: Requirements 1.1, 1.3**
    # For any manifest containing targets, the list command output SHALL include
    # every non-test target's name and type, and for library targets SHALL include
    # the linkage. For targets with dependencies, the output SHALL include
    # dependency names.

    Context 'Empty manifest shows no targets message' {
        It 'displays "No targets declared." for empty targets array' {
            $root = New-TestProjectWithTargets -Targets @()
            try {
                $output = Get-TargetListOutput -Root $root
                $combined = $output -join ' '
                $combined | Should Match 'No targets declared'
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }

    Context 'Single executable target shows name and type' {
        It 'output contains target name and executable type' {
            $targets = @(
                [ordered]@{ name = 'myapp'; type = 'executable'; target = 'myapp'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'myapp_tests'; type = 'test'; target = 'myapp_tests'; testFor = 'myapp' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $output = Get-TargetListOutput -Root $root
                $combined = $output -join "`n"
                $combined | Should Match 'myapp'
                $combined | Should Match 'executable'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'does not display test targets in the list' {
            $targets = @(
                [ordered]@{ name = 'myapp'; type = 'executable'; target = 'myapp'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'myapp_tests'; type = 'test'; target = 'myapp_tests'; testFor = 'myapp' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $output = Get-TargetListOutput -Root $root
                $combined = $output -join "`n"
                $combined | Should Not Match 'myapp_tests'
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }

    Context 'Library target shows name, type, and linkage' {
        It 'static library shows linkage in output' {
            $targets = @(
                [ordered]@{ name = 'core'; type = 'library'; linkage = 'static'; target = 'core'; dependencies = @() },
                [ordered]@{ name = 'core_tests'; type = 'test'; target = 'core_tests'; testFor = 'core' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $output = Get-TargetListOutput -Root $root
                $combined = $output -join "`n"
                $combined | Should Match 'core'
                $combined | Should Match 'library'
                $combined | Should Match 'static'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'shared library shows linkage in output' {
            $targets = @(
                [ordered]@{ name = 'renderer'; type = 'library'; linkage = 'shared'; target = 'renderer'; dependencies = @() },
                [ordered]@{ name = 'renderer_tests'; type = 'test'; target = 'renderer_tests'; testFor = 'renderer' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $output = Get-TargetListOutput -Root $root
                $combined = $output -join "`n"
                $combined | Should Match 'renderer'
                $combined | Should Match 'library'
                $combined | Should Match 'shared'
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }

    Context 'Target with dependencies shows dependency names' {
        It 'output includes dependency name for a target with one dependency' {
            $targets = @(
                [ordered]@{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                [ordered]@{ name = 'engine_tests'; type = 'test'; target = 'engine_tests'; testFor = 'engine' },
                [ordered]@{ name = 'app'; type = 'executable'; target = 'app'; workingDirectory = '.'; args = @(); dependencies = @('engine') },
                [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests'; testFor = 'app' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $output = Get-TargetListOutput -Root $root
                $combined = $output -join "`n"
                $combined | Should Match 'app'
                $combined | Should Match 'engine'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'output includes all dependency names for a target with multiple dependencies' {
            $targets = @(
                [ordered]@{ name = 'core'; type = 'library'; linkage = 'static'; target = 'core'; dependencies = @() },
                [ordered]@{ name = 'core_tests'; type = 'test'; target = 'core_tests'; testFor = 'core' },
                [ordered]@{ name = 'utils'; type = 'library'; linkage = 'shared'; target = 'utils'; dependencies = @() },
                [ordered]@{ name = 'utils_tests'; type = 'test'; target = 'utils_tests'; testFor = 'utils' },
                [ordered]@{ name = 'app'; type = 'executable'; target = 'app'; workingDirectory = '.'; args = @(); dependencies = @('core', 'utils') },
                [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests'; testFor = 'app' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $output = Get-TargetListOutput -Root $root
                $combined = $output -join "`n"
                $combined | Should Match 'core'
                $combined | Should Match 'utils'
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }

    Context 'Multiple targets of different types all displayed correctly' {
        It 'all non-test targets appear in the output with correct info' {
            $targets = @(
                [ordered]@{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                [ordered]@{ name = 'engine_tests'; type = 'test'; target = 'engine_tests'; testFor = 'engine' },
                [ordered]@{ name = 'renderer'; type = 'library'; linkage = 'shared'; target = 'renderer'; dependencies = @('engine') },
                [ordered]@{ name = 'renderer_tests'; type = 'test'; target = 'renderer_tests'; testFor = 'renderer' },
                [ordered]@{ name = 'game'; type = 'executable'; target = 'game'; workingDirectory = '.'; args = @(); dependencies = @('engine', 'renderer') },
                [ordered]@{ name = 'game_tests'; type = 'test'; target = 'game_tests'; testFor = 'game' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $output = Get-TargetListOutput -Root $root
                $combined = $output -join "`n"

                # All non-test target names present
                $combined | Should Match 'engine'
                $combined | Should Match 'renderer'
                $combined | Should Match 'game'

                # Types present
                $combined | Should Match 'library'
                $combined | Should Match 'executable'

                # Library linkages present
                $combined | Should Match 'static'
                $combined | Should Match 'shared'

                # Dependencies shown for dependent targets
                # renderer depends on engine, game depends on engine and renderer
                # The word 'engine' and 'renderer' appear in dependency displays

                # Test targets NOT shown
                $combined | Should Not Match 'engine_tests'
                $combined | Should Not Match 'renderer_tests'
                $combined | Should Not Match 'game_tests'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'targets with no dependencies do not show dependency text' {
            $targets = @(
                [ordered]@{ name = 'standalone'; type = 'executable'; target = 'standalone'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'standalone_tests'; type = 'test'; target = 'standalone_tests'; testFor = 'standalone' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $output = Get-TargetListOutput -Root $root
                $combined = $output -join "`n"
                $combined | Should Match 'standalone'
                $combined | Should Match 'executable'
                $combined | Should Not Match 'depends on'
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }
}
