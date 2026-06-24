$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoTargets.ps1"

# Helper: Creates a minimal temp project with empty targets array
function New-TestProject {
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
        targets = @()
    }

    $manifestPath = Join-Path $tempDir 'project.cdo.json'
    $json = $manifest | ConvertTo-Json -Depth 32
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($manifestPath, ($json + [Environment]::NewLine), $utf8)

    return $tempDir
}

# Helper: Creates a temp project with a pre-built targets array (avoids ordering issues from Invoke-CdoTargetCreate)
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

    # Create minimal src directories so CMake generation doesn't fail
    foreach ($t in $Targets) {
        $name = $t.name
        $type = $t.type
        if ($type -eq 'test') { continue }

        $srcDir = Join-Path $tempDir (Join-Path 'src' $name)
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        $srcFile = Join-Path $srcDir "$name.cpp"
        [System.IO.File]::WriteAllText($srcFile, "// $name source`n")

        if ($type -eq 'library') {
            $includeDir = Join-Path $tempDir (Join-Path 'include' $name)
            New-Item -ItemType Directory -Path $includeDir -Force | Out-Null
            $headerFile = Join-Path $includeDir "$name.h"
            [System.IO.File]::WriteAllText($headerFile, "// $name header`n")
        }

        $testDir = Join-Path $tempDir (Join-Path 'tests' $name)
        New-Item -ItemType Directory -Path $testDir -Force | Out-Null
        $testFile = Join-Path $testDir "test_$name.cpp"
        [System.IO.File]::WriteAllText($testFile, "// $name test`n")
    }

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

Describe 'Property 7: Target deletion removes manifest entry and preserves others' {
    # **Validates: Requirements 4.1**
    # For any manifest with multiple targets and for any target that has no
    # dependents, deleting that target SHALL remove exactly that target and its
    # associated test target from the manifest, leaving all other entries unchanged.

    Context 'Deleting a target with no dependents' {
        It 'removes the target entry from the manifest' {
            $targets = @(
                [ordered]@{ name = 'alpha'; type = 'executable'; target = 'alpha'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'alpha_tests'; type = 'test'; target = 'alpha_tests'; testFor = 'alpha' },
                [ordered]@{ name = 'beta'; type = 'library'; linkage = 'static'; target = 'beta'; dependencies = @() },
                [ordered]@{ name = 'beta_tests'; type = 'test'; target = 'beta_tests'; testFor = 'beta' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                Invoke-CdoTargetDelete -Root $root -Name 'alpha'

                $config = Get-TestManifest -Root $root
                $remaining = @($config.targets)
                $remaining | Where-Object { $_.name -eq 'alpha' } | Should BeNullOrEmpty
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'removes the associated test target entry' {
            $targets = @(
                [ordered]@{ name = 'alpha'; type = 'executable'; target = 'alpha'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'alpha_tests'; type = 'test'; target = 'alpha_tests'; testFor = 'alpha' },
                [ordered]@{ name = 'beta'; type = 'library'; linkage = 'static'; target = 'beta'; dependencies = @() },
                [ordered]@{ name = 'beta_tests'; type = 'test'; target = 'beta_tests'; testFor = 'beta' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                Invoke-CdoTargetDelete -Root $root -Name 'alpha'

                $config = Get-TestManifest -Root $root
                $remaining = @($config.targets)
                $remaining | Where-Object { $_.name -eq 'alpha_tests' } | Should BeNullOrEmpty
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'preserves all other target entries unchanged' {
            $targets = @(
                [ordered]@{ name = 'alpha'; type = 'executable'; target = 'alpha'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'alpha_tests'; type = 'test'; target = 'alpha_tests'; testFor = 'alpha' },
                [ordered]@{ name = 'beta'; type = 'library'; linkage = 'static'; target = 'beta'; dependencies = @() },
                [ordered]@{ name = 'beta_tests'; type = 'test'; target = 'beta_tests'; testFor = 'beta' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                Invoke-CdoTargetDelete -Root $root -Name 'alpha'

                $config = Get-TestManifest -Root $root
                $remaining = @($config.targets)
                $remaining.Count | Should Be 2

                $betaTarget = $remaining | Where-Object { $_.name -eq 'beta' }
                $betaTarget | Should Not BeNullOrEmpty
                $betaTarget.type | Should Be 'library'
                $betaTarget.linkage | Should Be 'static'

                $betaTest = $remaining | Where-Object { $_.name -eq 'beta_tests' }
                $betaTest | Should Not BeNullOrEmpty
                $betaTest.testFor | Should Be 'beta'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'deleting a library target removes exactly 2 entries and preserves others' {
            $targets = @(
                [ordered]@{ name = 'app'; type = 'executable'; target = 'app'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests'; testFor = 'app' },
                [ordered]@{ name = 'core'; type = 'library'; linkage = 'static'; target = 'core'; dependencies = @() },
                [ordered]@{ name = 'core_tests'; type = 'test'; target = 'core_tests'; testFor = 'core' },
                [ordered]@{ name = 'utils'; type = 'library'; linkage = 'shared'; target = 'utils'; dependencies = @() },
                [ordered]@{ name = 'utils_tests'; type = 'test'; target = 'utils_tests'; testFor = 'utils' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                Invoke-CdoTargetDelete -Root $root -Name 'core'

                $config = Get-TestManifest -Root $root
                $remaining = @($config.targets)
                # Started with 6, removed 2 (core + core_tests) = 4
                $remaining.Count | Should Be 4

                $remaining | Where-Object { $_.name -eq 'core' } | Should BeNullOrEmpty
                $remaining | Where-Object { $_.name -eq 'core_tests' } | Should BeNullOrEmpty

                # app and utils still present
                $remaining | Where-Object { $_.name -eq 'app' } | Should Not BeNullOrEmpty
                $remaining | Where-Object { $_.name -eq 'utils' } | Should Not BeNullOrEmpty
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'deleting the only target results in empty targets array' {
            $targets = @(
                [ordered]@{ name = 'solo'; type = 'executable'; target = 'solo'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'solo_tests'; type = 'test'; target = 'solo_tests'; testFor = 'solo' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                Invoke-CdoTargetDelete -Root $root -Name 'solo'

                $config = Get-TestManifest -Root $root
                $remaining = @($config.targets)
                $remaining.Count | Should Be 0
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'deletion works for various target names and types' {
            $cases = @(
                @{ DeleteName = 'game-engine'; Type = 'executable' },
                @{ DeleteName = 'my_lib'; Type = 'library' },
                @{ DeleteName = 'renderer'; Type = 'library' }
            )
            foreach ($case in $cases) {
                $deleteTarget = if ($case.Type -eq 'executable') {
                    [ordered]@{ name = $case.DeleteName; type = 'executable'; target = $case.DeleteName; workingDirectory = '.'; args = @(); dependencies = @() }
                } else {
                    [ordered]@{ name = $case.DeleteName; type = 'library'; linkage = 'static'; target = $case.DeleteName; dependencies = @() }
                }
                $targets = @(
                    $deleteTarget,
                    [ordered]@{ name = "$($case.DeleteName)_tests"; type = 'test'; target = "$($case.DeleteName)_tests"; testFor = $case.DeleteName },
                    [ordered]@{ name = 'keeper'; type = 'executable'; target = 'keeper'; workingDirectory = '.'; args = @(); dependencies = @() },
                    [ordered]@{ name = 'keeper_tests'; type = 'test'; target = 'keeper_tests'; testFor = 'keeper' }
                )
                $root = New-TestProjectWithTargets -Targets $targets
                try {
                    Invoke-CdoTargetDelete -Root $root -Name $case.DeleteName

                    $config = Get-TestManifest -Root $root
                    $remaining = @($config.targets)
                    $remaining.Count | Should Be 2
                    $remaining | Where-Object { $_.name -eq $case.DeleteName } | Should BeNullOrEmpty
                    $remaining | Where-Object { $_.name -eq 'keeper' } | Should Not BeNullOrEmpty
                } finally {
                    Remove-TestProject -Root $root
                }
            }
        }
    }
}

Describe 'Property 8: Deletion of depended-upon target is refused' {
    # **Validates: Requirements 4.4**
    # For any target that appears in another target's dependencies array,
    # attempting to delete it SHALL produce an error listing the dependent
    # targets, and the manifest SHALL remain unchanged.

    Context 'Refusing deletion when target has dependents' {
        It 'refuses to delete a target that another target depends on' {
            $targets = @(
                [ordered]@{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                [ordered]@{ name = 'engine_tests'; type = 'test'; target = 'engine_tests'; testFor = 'engine' },
                [ordered]@{ name = 'app'; type = 'executable'; target = 'app'; workingDirectory = '.'; args = @(); dependencies = @('engine') },
                [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests'; testFor = 'app' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                { Invoke-CdoTargetDelete -Root $root -Name 'engine' } |
                    Should Throw "depended on by"
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'error message lists the dependent target names' {
            $targets = @(
                [ordered]@{ name = 'core'; type = 'library'; linkage = 'static'; target = 'core'; dependencies = @() },
                [ordered]@{ name = 'core_tests'; type = 'test'; target = 'core_tests'; testFor = 'core' },
                [ordered]@{ name = 'app'; type = 'executable'; target = 'app'; workingDirectory = '.'; args = @(); dependencies = @('core') },
                [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests'; testFor = 'app' },
                [ordered]@{ name = 'tools'; type = 'executable'; target = 'tools'; workingDirectory = '.'; args = @(); dependencies = @('core') },
                [ordered]@{ name = 'tools_tests'; type = 'test'; target = 'tools_tests'; testFor = 'tools' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $errorThrown = $null
                try {
                    Invoke-CdoTargetDelete -Root $root -Name 'core'
                } catch {
                    $errorThrown = $_.Exception.Message
                }

                $errorThrown | Should Not BeNullOrEmpty
                $errorThrown | Should Match 'app'
                $errorThrown | Should Match 'tools'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'manifest remains completely unchanged after refused deletion' {
            $targets = @(
                [ordered]@{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                [ordered]@{ name = 'engine_tests'; type = 'test'; target = 'engine_tests'; testFor = 'engine' },
                [ordered]@{ name = 'app'; type = 'executable'; target = 'app'; workingDirectory = '.'; args = @(); dependencies = @('engine') },
                [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests'; testFor = 'app' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $manifestPath = Join-Path $root 'project.cdo.json'
                $beforeContent = Get-Content -LiteralPath $manifestPath -Raw

                try {
                    Invoke-CdoTargetDelete -Root $root -Name 'engine'
                } catch {
                    # Expected to throw
                }

                $afterContent = Get-Content -LiteralPath $manifestPath -Raw
                $afterContent | Should Be $beforeContent
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'target with single dependent is refused with correct message' {
            $targets = @(
                [ordered]@{ name = 'base'; type = 'library'; linkage = 'shared'; target = 'base'; dependencies = @() },
                [ordered]@{ name = 'base_tests'; type = 'test'; target = 'base_tests'; testFor = 'base' },
                [ordered]@{ name = 'consumer'; type = 'executable'; target = 'consumer'; workingDirectory = '.'; args = @(); dependencies = @('base') },
                [ordered]@{ name = 'consumer_tests'; type = 'test'; target = 'consumer_tests'; testFor = 'consumer' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $errorThrown = $null
                try {
                    Invoke-CdoTargetDelete -Root $root -Name 'base'
                } catch {
                    $errorThrown = $_.Exception.Message
                }

                $errorThrown | Should Not BeNullOrEmpty
                $errorThrown | Should Match 'consumer'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'allows deleting a target after its dependents are removed' {
            $targets = @(
                [ordered]@{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                [ordered]@{ name = 'engine_tests'; type = 'test'; target = 'engine_tests'; testFor = 'engine' },
                [ordered]@{ name = 'app'; type = 'executable'; target = 'app'; workingDirectory = '.'; args = @(); dependencies = @('engine') },
                [ordered]@{ name = 'app_tests'; type = 'test'; target = 'app_tests'; testFor = 'app' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                # First delete the dependent
                Invoke-CdoTargetDelete -Root $root -Name 'app'

                # Now deleting engine should succeed
                { Invoke-CdoTargetDelete -Root $root -Name 'engine' } | Should Not Throw

                $config = Get-TestManifest -Root $root
                $remaining = @($config.targets)
                $remaining.Count | Should Be 0
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }
}

Describe 'Property 16: Delete non-existent target reports available targets' {
    # **Validates: Requirements 4.3**
    # For any manifest and for any name that does not match an existing target,
    # attempting to delete SHALL produce an error that includes all available
    # target names.

    Context 'Error message includes available target names' {
        It 'reports error for non-existent target name' {
            $targets = @(
                [ordered]@{ name = 'alpha'; type = 'executable'; target = 'alpha'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'alpha_tests'; type = 'test'; target = 'alpha_tests'; testFor = 'alpha' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                { Invoke-CdoTargetDelete -Root $root -Name 'nonexistent' } |
                    Should Throw "not found"
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'error message lists all available non-test target names' {
            $targets = @(
                [ordered]@{ name = 'alpha'; type = 'executable'; target = 'alpha'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'alpha_tests'; type = 'test'; target = 'alpha_tests'; testFor = 'alpha' },
                [ordered]@{ name = 'beta'; type = 'library'; linkage = 'static'; target = 'beta'; dependencies = @() },
                [ordered]@{ name = 'beta_tests'; type = 'test'; target = 'beta_tests'; testFor = 'beta' },
                [ordered]@{ name = 'gamma'; type = 'library'; linkage = 'shared'; target = 'gamma'; dependencies = @() },
                [ordered]@{ name = 'gamma_tests'; type = 'test'; target = 'gamma_tests'; testFor = 'gamma' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $errorThrown = $null
                try {
                    Invoke-CdoTargetDelete -Root $root -Name 'nosuch'
                } catch {
                    $errorThrown = $_.Exception.Message
                }

                $errorThrown | Should Not BeNullOrEmpty
                $errorThrown | Should Match 'alpha'
                $errorThrown | Should Match 'beta'
                $errorThrown | Should Match 'gamma'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'does not list test target names in available targets' {
            $targets = @(
                [ordered]@{ name = 'myapp'; type = 'executable'; target = 'myapp'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'myapp_tests'; type = 'test'; target = 'myapp_tests'; testFor = 'myapp' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $errorThrown = $null
                try {
                    Invoke-CdoTargetDelete -Root $root -Name 'bogus'
                } catch {
                    $errorThrown = $_.Exception.Message
                }

                $errorThrown | Should Not BeNullOrEmpty
                $errorThrown | Should Match 'myapp'
                # Should NOT contain test target names in the available list
                $errorThrown | Should Not Match 'myapp_tests'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'manifest is unchanged after failed delete of non-existent target' {
            $targets = @(
                [ordered]@{ name = 'first'; type = 'executable'; target = 'first'; workingDirectory = '.'; args = @(); dependencies = @() },
                [ordered]@{ name = 'first_tests'; type = 'test'; target = 'first_tests'; testFor = 'first' }
            )
            $root = New-TestProjectWithTargets -Targets $targets
            try {
                $manifestPath = Join-Path $root 'project.cdo.json'
                $beforeContent = Get-Content -LiteralPath $manifestPath -Raw

                try {
                    Invoke-CdoTargetDelete -Root $root -Name 'doesnotexist'
                } catch {
                    # Expected to throw
                }

                $afterContent = Get-Content -LiteralPath $manifestPath -Raw
                $afterContent | Should Be $beforeContent
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'error includes target name in message for various invalid names' {
            $invalidNames = @('missing', 'unknown-target', 'xyz123')
            $targets = @(
                [ordered]@{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                [ordered]@{ name = 'engine_tests'; type = 'test'; target = 'engine_tests'; testFor = 'engine' }
            )
            foreach ($badName in $invalidNames) {
                $root = New-TestProjectWithTargets -Targets $targets
                try {
                    $errorThrown = $null
                    try {
                        Invoke-CdoTargetDelete -Root $root -Name $badName
                    } catch {
                        $errorThrown = $_.Exception.Message
                    }

                    $errorThrown | Should Not BeNullOrEmpty
                    $errorThrown | Should Match $badName
                    $errorThrown | Should Match 'engine'
                } finally {
                    Remove-TestProject -Root $root
                }
            }
        }
    }
}
