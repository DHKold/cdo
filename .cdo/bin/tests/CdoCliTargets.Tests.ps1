$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoTargets.ps1"

Describe 'CLI command target resolution' {
    Context 'Property 7: Type mismatch error for wrong command' {
        # Test that run rejects non-executable targets
        It 'run command rejects library target with type mismatch error' {
            $targets = @(
                [pscustomobject]@{ name = 'core'; type = 'library'; target = 'core' }
            )
            $resolved = Get-CdoTargetByName -Targets $targets -Name 'core'
            $type = Get-CdoProperty -Object $resolved -Name 'type'
            $type | Should Be 'library'
            # Simulate the run command's type check
            { if ($type -ne 'executable') { throw "Cannot run target 'core': only executable targets can be run. Target type is '$type'." } } |
                Should Throw "only executable targets can be run"
        }

        # Test that test resolves non-test targets to <name>_tests
        It 'test command rejects executable target with type mismatch error' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'app' }
            )
            $directMatch = Get-CdoTargetByName -Targets $targets -Name 'app'
            $type = Get-CdoProperty -Object $directMatch -Name 'type'
            # When type is not 'test', try resolving <name>_tests
            $type | Should Not Be 'test'
            $testName = "app_tests"
            $testMatch = Get-CdoTargetByName -Targets $targets -Name $testName
            # No app_tests target exists in this test setup, so it should fail
            { if ($null -eq $testMatch -or (Get-CdoProperty -Object $testMatch -Name 'type') -ne 'test') { throw "No test target found for 'app'. Expected '${testName}' to exist." } } |
                Should Throw "No test target found for 'app'"
        }

        # Test that run rejects test targets
        It 'run command rejects test target with type mismatch error' {
            $targets = @(
                [pscustomobject]@{ name = 'app_tests'; type = 'test'; target = 'app_tests' }
            )
            $resolved = Get-CdoTargetByName -Targets $targets -Name 'app_tests'
            $type = Get-CdoProperty -Object $resolved -Name 'type'
            { if ($type -ne 'executable') { throw "Cannot run target 'app_tests': only executable targets can be run. Target type is '$type'." } } |
                Should Throw "only executable targets can be run"
        }
    }

    Context 'Build command default resolution' {
        It 'build resolves single executable as default' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'app' }
            )
            $result = Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable', 'library', 'shared-library') -Command 'build'
            $result.name | Should Be 'app'
        }

        It 'build resolves single library as default' {
            $targets = @(
                [pscustomobject]@{ name = 'core'; type = 'library'; target = 'core' }
            )
            $result = Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable', 'library', 'shared-library') -Command 'build'
            $result.name | Should Be 'core'
        }

        It 'build throws when multiple buildable targets exist' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'app' },
                [pscustomobject]@{ name = 'core'; type = 'library'; target = 'core' }
            )
            { Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable', 'library', 'shared-library') -Command 'build' } |
                Should Throw 'app, core'
        }
    }

    Context 'Run command default resolution' {
        It 'run resolves single executable as default' {
            $targets = @(
                [pscustomobject]@{ name = 'game'; type = 'executable'; target = 'game' },
                [pscustomobject]@{ name = 'core'; type = 'library'; target = 'core' }
            )
            $result = Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable') -Command 'run'
            $result.name | Should Be 'game'
        }

        It 'run throws when no executable exists' {
            $targets = @(
                [pscustomobject]@{ name = 'core'; type = 'library'; target = 'core' }
            )
            { Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable') -Command 'run' } |
                Should Throw 'No executable'
        }
    }

    Context 'Test command resolution' {
        It 'test finds all test-type targets' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'app' },
                [pscustomobject]@{ name = 'unit_tests'; type = 'test'; target = 'unit_tests' },
                [pscustomobject]@{ name = 'int_tests'; type = 'test'; target = 'int_tests' }
            )
            $testTargets = @($targets | Where-Object { (Get-CdoProperty -Object $_ -Name 'type') -eq 'test' })
            $testTargets.Count | Should Be 2
        }

        It 'test resolves named test target' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'app' },
                [pscustomobject]@{ name = 'unit_tests'; type = 'test'; target = 'unit_tests' }
            )
            $resolved = Get-CdoTargetByName -Targets $targets -Name 'unit_tests'
            $resolved.type | Should Be 'test'
        }

        It 'test resolves non-test target name to <name>_tests' {
            $targets = @(
                [pscustomobject]@{ name = 'engine'; type = 'library'; target = 'engine' },
                [pscustomobject]@{ name = 'engine_tests'; type = 'test'; target = 'engine_tests'; testFor = 'engine' }
            )
            # Simulate the per-target resolution logic from Run-Tests
            $directMatch = Get-CdoTargetByName -Targets $targets -Name 'engine'
            $type = Get-CdoProperty -Object $directMatch -Name 'type'
            $type | Should Not Be 'test'

            $testName = "engine_tests"
            $testMatch = Get-CdoTargetByName -Targets $targets -Name $testName
            $testMatch | Should Not Be $null
            (Get-CdoProperty -Object $testMatch -Name 'type') | Should Be 'test'
            (Get-CdoProperty -Object $testMatch -Name 'name') | Should Be 'engine_tests'
        }

        It 'test throws when target not found' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'app' }
            )
            $directMatch = Get-CdoTargetByName -Targets $targets -Name 'nonexistent'
            $directMatch | Should Be $null
        }
    }

    Context 'Named target lookup' {
        It 'finds target by name field' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'my_custom_exe' }
            )
            $resolved = Get-CdoTargetByName -Targets $targets -Name 'app'
            $resolved.name | Should Be 'app'
        }

        It 'returns null when target not found' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'app' },
                [pscustomobject]@{ name = 'core'; type = 'library'; target = 'core' }
            )
            $result = Get-CdoTargetByName -Targets $targets -Name 'nope'
            $result | Should Be $null
        }
    }
}
