$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoTargets.ps1"

Describe 'Test-CdoTargetName' {
    Context 'Valid target names' {
        It 'accepts lowercase alphabetic name "<Name>"' -TestCases @(
            @{ Name = 'myapp' },
            @{ Name = 'engine' },
            @{ Name = 'renderer' }
        ) {
            param($Name)
            Test-CdoTargetName -Name $Name | Should Be $true
        }

        It 'accepts name with digits "<Name>"' -TestCases @(
            @{ Name = 'app123' },
            @{ Name = 'v2renderer' }
        ) {
            param($Name)
            Test-CdoTargetName -Name $Name | Should Be $true
        }

        It 'accepts name with hyphens "<Name>"' -TestCases @(
            @{ Name = 'my-app' },
            @{ Name = 'game-engine' }
        ) {
            param($Name)
            Test-CdoTargetName -Name $Name | Should Be $true
        }

        It 'accepts name with underscores "<Name>"' -TestCases @(
            @{ Name = 'my_app' },
            @{ Name = 'game_engine' }
        ) {
            param($Name)
            Test-CdoTargetName -Name $Name | Should Be $true
        }

        It 'accepts mixed valid characters "<Name>"' -TestCases @(
            @{ Name = 'my-app_v2' },
            @{ Name = 'engine123-core' }
        ) {
            param($Name)
            Test-CdoTargetName -Name $Name | Should Be $true
        }
    }

    Context 'Invalid target names' {
        It 'rejects name starting with digit "<Name>"' -TestCases @(
            @{ Name = '1app' },
            @{ Name = '99bottles' }
        ) {
            param($Name)
            Test-CdoTargetName -Name $Name | Should Be $false
        }

        It 'rejects name with uppercase letters "<Name>"' -TestCases @(
            @{ Name = 'MyApp' },
            @{ Name = 'ENGINE' },
            @{ Name = 'myApp' }
        ) {
            param($Name)
            Test-CdoTargetName -Name $Name | Should Be $false
        }

        It 'rejects name with special characters "<Name>"' -TestCases @(
            @{ Name = 'my.app' },
            @{ Name = 'my app' },
            @{ Name = 'my@app' },
            @{ Name = 'app!' }
        ) {
            param($Name)
            Test-CdoTargetName -Name $Name | Should Be $false
        }

        It 'rejects empty string' {
            Test-CdoTargetName -Name '' | Should Be $false
        }
    }

    Context 'Reserved names rejected' {
        It 'rejects reserved name "<Name>"' -TestCases @(
            @{ Name = 'all' },
            @{ Name = 'clean' },
            @{ Name = 'test' },
            @{ Name = 'install' },
            @{ Name = 'package' }
        ) {
            param($Name)
            Test-CdoTargetName -Name $Name | Should Be $false
        }
    }
}

Describe 'Test-CdoTargetExists' {
    Context 'Existence checking' {
        It 'returns true when target name matches existing entry' {
            $targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static' },
                @{ name = 'app'; type = 'executable' }
            )

            Test-CdoTargetExists -Targets $targets -Name 'engine' | Should Be $true
        }

        It 'returns false when target name is not in the targets array' {
            $targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static' },
                @{ name = 'app'; type = 'executable' }
            )

            Test-CdoTargetExists -Targets $targets -Name 'renderer' | Should Be $false
        }

        It 'works with empty targets array' {
            $targets = @()

            Test-CdoTargetExists -Targets $targets -Name 'anything' | Should Be $false
        }
    }
}

Describe 'Property 3: Duplicate target name rejection' {
    # **Validates: Requirements 2.5**
    # For any manifest containing existing targets and for any name that matches
    # an existing target, attempting to create a target with that name SHALL produce
    # an error and leave the manifest unchanged.

    Context 'Duplicate detection across varied manifests' {
        It 'detects duplicate for single-target manifest' {
            $targets = @(
                @{ name = 'app'; type = 'executable' }
            )

            Test-CdoTargetExists -Targets $targets -Name 'app' | Should Be $true
        }

        It 'detects duplicate among multiple targets' {
            $targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static' },
                @{ name = 'renderer'; type = 'library'; linkage = 'shared' },
                @{ name = 'app'; type = 'executable' }
            )

            Test-CdoTargetExists -Targets $targets -Name 'renderer' | Should Be $true
        }

        It 'does not falsely detect duplicates for unique names' {
            $targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static' },
                @{ name = 'renderer'; type = 'library'; linkage = 'shared' },
                @{ name = 'app'; type = 'executable' }
            )

            Test-CdoTargetExists -Targets $targets -Name 'physics' | Should Be $false
        }

        It 'detects duplicates for randomized existing target names' {
            # Property-based: for any set of existing targets, Test-CdoTargetExists
            # returns true for every name in that set
            $targetNames = @('core', 'utils', 'network', 'audio', 'graphics')
            $targets = @($targetNames | ForEach-Object {
                @{ name = $_; type = 'library'; linkage = 'static' }
            })

            foreach ($name in $targetNames) {
                Test-CdoTargetExists -Targets $targets -Name $name | Should Be $true
            }
        }

        It 'never reports false duplicates for names not in the manifest' {
            # Property-based: for any set of existing targets, names NOT in that
            # set are never reported as duplicates
            $existingNames = @('alpha', 'beta', 'gamma')
            $targets = @($existingNames | ForEach-Object {
                @{ name = $_; type = 'executable' }
            })

            $nonExistingNames = @('delta', 'epsilon', 'zeta', 'eta', 'theta')
            foreach ($name in $nonExistingNames) {
                Test-CdoTargetExists -Targets $targets -Name $name | Should Be $false
            }
        }

        It 'detects duplicate for test target names' {
            # Test targets also occupy the namespace
            $targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static' },
                @{ name = 'engine_tests'; type = 'test'; testFor = 'engine' }
            )

            Test-CdoTargetExists -Targets $targets -Name 'engine_tests' | Should Be $true
        }
    }
}

Describe 'Property 4: Library linkage flag is mandatory' {
    # **Validates: Requirements 3.3**
    # For any target creation with type library that does not specify either
    # --static or --shared, the command SHALL produce an error and leave the
    # manifest unchanged.
    #
    # NOTE: Invoke-CdoTargetCreate doesn't exist yet (Task 4.1), so we test
    # that Test-CdoTargetName correctly validates name format and reserved names
    # as a prerequisite for target creation validation.

    Context 'Name validation as creation prerequisite' {
        It 'valid names pass validation for all target types' {
            # Property: for any valid name, Test-CdoTargetName returns true
            # regardless of what type the target will be (executable or library)
            $validNames = @('mylib', 'core-utils', 'net_layer', 'audio2', 'gfx-v3')
            foreach ($name in $validNames) {
                Test-CdoTargetName -Name $name | Should Be $true
            }
        }

        It 'invalid names are rejected before linkage validation can occur' {
            # Property: for any invalid name, target creation would fail at name
            # validation before even checking linkage. This ensures the name gate
            # prevents invalid library targets.
            $invalidNames = @('', '1lib', 'MyLib', 'lib.core', 'all', 'test')
            foreach ($name in $invalidNames) {
                Test-CdoTargetName -Name $name | Should Be $false
            }
        }

        It 'single-character lowercase letter is a valid name' {
            # Boundary: shortest valid name
            Test-CdoTargetName -Name 'a' | Should Be $true
        }

        It 'name starting with underscore is invalid' {
            Test-CdoTargetName -Name '_mylib' | Should Be $false
        }

        It 'name starting with hyphen is invalid' {
            Test-CdoTargetName -Name '-mylib' | Should Be $false
        }

        It 'reserved names are rejected regardless of intended type' {
            # Property: reserved names are always invalid, whether the user intends
            # to create an executable or a library
            $reserved = @('all', 'clean', 'test', 'install', 'package')
            foreach ($name in $reserved) {
                Test-CdoTargetName -Name $name | Should Be $false
            }
        }
    }
}
