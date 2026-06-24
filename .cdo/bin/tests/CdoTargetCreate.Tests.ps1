$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoSkeletons.ps1"
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

Describe 'Property 1: Target creation produces correct manifest entry' {
    # **Validates: Requirements 2.1, 3.1, 3.2, 7.2**
    # For any valid target name and type (executable with no linkage, or library
    # with static/shared linkage), creating that target SHALL produce a manifest
    # entry whose name, type, and linkage fields exactly match the input parameters,
    # and the targets array length SHALL increase by exactly 2 (the target itself
    # plus its test target).

    Context 'Executable target creation manifest entries' {
        It 'creates executable target with correct name, type fields' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'myapp' -Type 'executable'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $target = $targets | Where-Object { $_.name -eq 'myapp' }

                $target | Should Not BeNullOrEmpty
                $target.name | Should Be 'myapp'
                $target.type | Should Be 'executable'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'increases targets array length by exactly 2 (target + test target)' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'game' -Type 'executable'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $targets.Count | Should Be 2
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates associated test target with correct testFor field' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'player' -Type 'executable'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $testTarget = $targets | Where-Object { $_.name -eq 'player_tests' }

                $testTarget | Should Not BeNullOrEmpty
                $testTarget.type | Should Be 'test'
                $testTarget.testFor | Should Be 'player'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'produces correct manifest for various valid executable names' {
            $names = @('alpha', 'game-engine', 'my_app', 'renderer2')
            foreach ($name in $names) {
                $root = New-TestProject
                try {
                    Invoke-CdoTargetCreate -Root $root -Name $name -Type 'executable'

                    $config = Get-TestManifest -Root $root
                    $targets = @($config.targets)

                    $targets.Count | Should Be 2
                    $target = $targets | Where-Object { $_.name -eq $name }
                    $target.type | Should Be 'executable'

                    $testTarget = $targets | Where-Object { $_.name -eq "${name}_tests" }
                    $testTarget.testFor | Should Be $name
                } finally {
                    Remove-TestProject -Root $root
                }
            }
        }
    }

    Context 'Static library target creation manifest entries' {
        It 'creates static library target with correct name, type, and linkage fields' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'engine' -Type 'library' -Linkage 'static'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $target = $targets | Where-Object { $_.name -eq 'engine' }

                $target | Should Not BeNullOrEmpty
                $target.name | Should Be 'engine'
                $target.type | Should Be 'library'
                $target.linkage | Should Be 'static'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'increases targets array length by exactly 2 for static library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'core' -Type 'library' -Linkage 'static'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $targets.Count | Should Be 2
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates associated test target for static library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'utils' -Type 'library' -Linkage 'static'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $testTarget = $targets | Where-Object { $_.name -eq 'utils_tests' }

                $testTarget | Should Not BeNullOrEmpty
                $testTarget.type | Should Be 'test'
                $testTarget.testFor | Should Be 'utils'
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }

    Context 'Shared library target creation manifest entries' {
        It 'creates shared library target with correct name, type, and linkage fields' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'renderer' -Type 'library' -Linkage 'shared'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $target = $targets | Where-Object { $_.name -eq 'renderer' }

                $target | Should Not BeNullOrEmpty
                $target.name | Should Be 'renderer'
                $target.type | Should Be 'library'
                $target.linkage | Should Be 'shared'
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'increases targets array length by exactly 2 for shared library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'audio' -Type 'library' -Linkage 'shared'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $targets.Count | Should Be 2
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates associated test target for shared library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'network' -Type 'library' -Linkage 'shared'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $testTarget = $targets | Where-Object { $_.name -eq 'network_tests' }

                $testTarget | Should Not BeNullOrEmpty
                $testTarget.type | Should Be 'test'
                $testTarget.testFor | Should Be 'network'
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }

    Context 'Error cases preserve manifest integrity' {
        It 'creating a target with duplicate name fails' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'app' -Type 'executable'

                { Invoke-CdoTargetCreate -Root $root -Name 'app' -Type 'executable' } |
                    Should Throw "already exists"

                # Manifest should still have exactly 2 entries from the first creation
                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $targets.Count | Should Be 2
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creating a library without linkage fails' {
            $root = New-TestProject
            try {
                { Invoke-CdoTargetCreate -Root $root -Name 'badlib' -Type 'library' -Linkage '' } |
                    Should Throw "require explicit linkage"

                # Manifest should remain empty
                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $targets.Count | Should Be 0
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'second target creation increases array from 2 to 4' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'first' -Type 'executable'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $targets.Count | Should Be 2

                Invoke-CdoTargetCreate -Root $root -Name 'second' -Type 'executable'

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                # Each creation adds 2 entries (target + test), so 2 * 2 = 4
                $targets.Count | Should Be 4
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }
}

Describe 'Property 2: Target creation produces correct directory structure' {
    # **Validates: Requirements 2.2, 2.3, 3.4, 3.5, 3.6, 6.1, 6.2, 7.1**
    # For any valid target name and type, creating that target SHALL produce
    # directories where: executables get src/<name>/ and tests/<name>/;
    # libraries additionally get include/<name>/. No other target's directories
    # are created or modified.

    Context 'Executable target directory structure' {
        It 'creates src/<name>/ directory' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'myapp' -Type 'executable'

                $srcDir = Join-Path $root (Join-Path 'src' 'myapp')
                Test-Path -LiteralPath $srcDir -PathType Container | Should Be $true
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates tests/<name>/ directory' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'myapp' -Type 'executable'

                $testDir = Join-Path $root (Join-Path 'tests' 'myapp')
                Test-Path -LiteralPath $testDir -PathType Container | Should Be $true
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'does NOT create include/<name>/ directory for executables' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'myapp' -Type 'executable'

                $includeDir = Join-Path $root (Join-Path 'include' 'myapp')
                Test-Path -LiteralPath $includeDir | Should Be $false
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates skeleton source file in src/<name>/' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'game' -Type 'executable'

                $srcDir = Join-Path $root (Join-Path 'src' 'game')
                $files = @(Get-ChildItem -LiteralPath $srcDir -File -ErrorAction SilentlyContinue)
                $files.Count | Should BeGreaterThan 0
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates skeleton test file in tests/<name>/' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'game' -Type 'executable'

                $testDir = Join-Path $root (Join-Path 'tests' 'game')
                $files = @(Get-ChildItem -LiteralPath $testDir -File -ErrorAction SilentlyContinue)
                $files.Count | Should BeGreaterThan 0
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'produces correct directories for various executable names' {
            $names = @('alpha', 'game-engine', 'my_app')
            foreach ($name in $names) {
                $root = New-TestProject
                try {
                    Invoke-CdoTargetCreate -Root $root -Name $name -Type 'executable'

                    $srcDir = Join-Path $root (Join-Path 'src' $name)
                    $testDir = Join-Path $root (Join-Path 'tests' $name)
                    $includeDir = Join-Path $root (Join-Path 'include' $name)

                    Test-Path -LiteralPath $srcDir -PathType Container | Should Be $true
                    Test-Path -LiteralPath $testDir -PathType Container | Should Be $true
                    Test-Path -LiteralPath $includeDir | Should Be $false
                } finally {
                    Remove-TestProject -Root $root
                }
            }
        }
    }

    Context 'Library target directory structure' {
        It 'creates src/<name>/ directory for library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'engine' -Type 'library' -Linkage 'static'

                $srcDir = Join-Path $root (Join-Path 'src' 'engine')
                Test-Path -LiteralPath $srcDir -PathType Container | Should Be $true
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates include/<name>/ directory for library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'engine' -Type 'library' -Linkage 'static'

                $includeDir = Join-Path $root (Join-Path 'include' 'engine')
                Test-Path -LiteralPath $includeDir -PathType Container | Should Be $true
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates tests/<name>/ directory for library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'engine' -Type 'library' -Linkage 'static'

                $testDir = Join-Path $root (Join-Path 'tests' 'engine')
                Test-Path -LiteralPath $testDir -PathType Container | Should Be $true
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates skeleton source file in src/<name>/ for library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'core' -Type 'library' -Linkage 'static'

                $srcDir = Join-Path $root (Join-Path 'src' 'core')
                $files = @(Get-ChildItem -LiteralPath $srcDir -File -ErrorAction SilentlyContinue)
                $files.Count | Should BeGreaterThan 0
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates skeleton header file in include/<name>/ for library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'core' -Type 'library' -Linkage 'static'

                $includeDir = Join-Path $root (Join-Path 'include' 'core')
                $files = @(Get-ChildItem -LiteralPath $includeDir -File -ErrorAction SilentlyContinue)
                $files.Count | Should BeGreaterThan 0
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'creates skeleton test file in tests/<name>/ for library' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'core' -Type 'library' -Linkage 'static'

                $testDir = Join-Path $root (Join-Path 'tests' 'core')
                $files = @(Get-ChildItem -LiteralPath $testDir -File -ErrorAction SilentlyContinue)
                $files.Count | Should BeGreaterThan 0
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'shared library also creates include/<name>/ directory' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'renderer' -Type 'library' -Linkage 'shared'

                $srcDir = Join-Path $root (Join-Path 'src' 'renderer')
                $includeDir = Join-Path $root (Join-Path 'include' 'renderer')
                $testDir = Join-Path $root (Join-Path 'tests' 'renderer')

                Test-Path -LiteralPath $srcDir -PathType Container | Should Be $true
                Test-Path -LiteralPath $includeDir -PathType Container | Should Be $true
                Test-Path -LiteralPath $testDir -PathType Container | Should Be $true
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'produces correct directories for various library names and linkages' {
            $cases = @(
                @{ Name = 'physics'; Linkage = 'static' },
                @{ Name = 'net-layer'; Linkage = 'shared' },
                @{ Name = 'audio_sys'; Linkage = 'static' }
            )
            foreach ($case in $cases) {
                $root = New-TestProject
                try {
                    Invoke-CdoTargetCreate -Root $root -Name $case.Name -Type 'library' -Linkage $case.Linkage

                    $srcDir = Join-Path $root (Join-Path 'src' $case.Name)
                    $includeDir = Join-Path $root (Join-Path 'include' $case.Name)
                    $testDir = Join-Path $root (Join-Path 'tests' $case.Name)

                    Test-Path -LiteralPath $srcDir -PathType Container | Should Be $true
                    Test-Path -LiteralPath $includeDir -PathType Container | Should Be $true
                    Test-Path -LiteralPath $testDir -PathType Container | Should Be $true
                } finally {
                    Remove-TestProject -Root $root
                }
            }
        }
    }

    Context 'No other target directories are affected' {
        It 'creating a second target does not modify the first targets directories' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'first' -Type 'executable'

                # Record state of first target's directory
                $firstSrcDir = Join-Path $root (Join-Path 'src' 'first')
                $firstFilesBefore = @(Get-ChildItem -LiteralPath $firstSrcDir -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })

                # Create a second target
                Invoke-CdoTargetCreate -Root $root -Name 'second' -Type 'library' -Linkage 'static'

                # First target's directories should be unchanged
                $firstFilesAfter = @(Get-ChildItem -LiteralPath $firstSrcDir -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
                $firstFilesAfter.Count | Should Be $firstFilesBefore.Count

                # Second target should NOT have files in first target's directory
                $secondInFirst = Join-Path $firstSrcDir 'second.cpp'
                Test-Path -LiteralPath $secondInFirst | Should Be $false
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'executable target does not create directories belonging to other targets' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'app' -Type 'executable'

                # Should only have src/app and tests/app, not any other target dirs
                $srcDirs = @(Get-ChildItem -LiteralPath (Join-Path $root 'src') -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
                $srcDirs.Count | Should Be 1
                $srcDirs[0] | Should Be 'app'
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }

    Context 'CMakeLists.txt generation' {
        It 'creates CMakeLists.txt in the project root after target creation' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'app' -Type 'executable'

                $cmakePath = Join-Path $root 'CMakeLists.txt'
                Test-Path -LiteralPath $cmakePath | Should Be $true
            } finally {
                Remove-TestProject -Root $root
            }
        }

        It 'CMakeLists.txt references the new target' {
            $root = New-TestProject
            try {
                Invoke-CdoTargetCreate -Root $root -Name 'myapp' -Type 'executable'

                $cmakePath = Join-Path $root 'CMakeLists.txt'
                $cmakeContent = Get-Content -LiteralPath $cmakePath -Raw
                $cmakeContent | Should Match 'add_executable\(myapp'
            } finally {
                Remove-TestProject -Root $root
            }
        }
    }
}
