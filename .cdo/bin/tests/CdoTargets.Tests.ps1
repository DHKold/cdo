$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"

Describe 'Resolve-CdoTargets' {
    Context 'Property 1: Target parsing preserves defaults' {
        # **Validates: Requirements 1.2**

        It 'target field defaults to name when not specified' {
            $config = @{
                targets = @(
                    @{ name = 'myapp'; type = 'executable' }
                )
            }

            $result = Resolve-CdoTargets -Config $config
            $result[0].target | Should Be 'myapp'
        }

        It 'preserves explicit target field when specified' {
            $config = @{
                targets = @(
                    @{ name = 'app'; type = 'executable'; target = 'my_custom_exe' }
                )
            }

            $result = Resolve-CdoTargets -Config $config
            $result[0].target | Should Be 'my_custom_exe'
            $result[0].name | Should Be 'app'
        }

        It 'defaults target to name for randomized inputs' {
            $names = @('alpha', 'beta_lib', 'gamma123', 'deltaApp', 'epsilon_test')
            foreach ($name in $names) {
                $config = @{
                    targets = @(
                        @{ name = $name; type = 'executable' }
                    )
                }
                $result = Resolve-CdoTargets -Config $config
                $result[0].target | Should Be $name
            }
        }
    }

    Context 'Property 2: Legacy manifest produces equivalent targets' {
        # **Validates: Requirements 1.3**

        It 'translates apps entries to targets with correct types' {
            $config = @{
                apps = @(
                    @{ name = 'game'; type = 'executable' },
                    @{ name = 'core'; type = 'library' }
                )
            }

            $result = Resolve-CdoTargets -Config $config
            $result.Count | Should Be 2
            $result[0].name | Should Be 'game'
            $result[0].type | Should Be 'executable'
            $result[1].name | Should Be 'core'
            $result[1].type | Should Be 'library'
        }

        It 'defaults legacy app type to executable when not specified' {
            $config = @{
                apps = @(
                    @{ name = 'simpleapp' }
                )
            }

            $result = Resolve-CdoTargets -Config $config
            $result[0].type | Should Be 'executable'
        }

        It 'translates enabled tests to test-type targets' {
            $config = @{
                apps = @(
                    @{ name = 'app'; type = 'executable' }
                )
                tests = @{ enabled = $true; target = 'app_tests' }
            }

            $result = Resolve-CdoTargets -Config $config
            $testTarget = $result | Where-Object { $_.type -eq 'test' }
            $testTarget | Should Not BeNullOrEmpty
            $testTarget.name | Should Be 'app_tests'
            $testTarget.type | Should Be 'test'
        }

        It 'excludes disabled tests' {
            $config = @{
                apps = @(
                    @{ name = 'app'; type = 'executable' }
                )
                tests = @{ enabled = $false; target = 'app_tests' }
            }

            $result = Resolve-CdoTargets -Config $config
            $testTarget = $result | Where-Object { $_.type -eq 'test' }
            $testTarget | Should BeNullOrEmpty
        }
    }
}

Describe 'Resolve-CdoDefaultTarget' {
    Context 'Property 4: Single-target default resolution' {
        # **Validates: Requirements 2.2, 3.2, 4.2**

        It 'returns the single matching target without error' {
            $targets = @(
                [pscustomobject]@{ name = 'myapp'; type = 'executable'; target = 'myapp' }
            )

            $result = Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable') -Command 'run'
            $result.name | Should Be 'myapp'
        }

        It 'returns single library target for build command' {
            $targets = @(
                [pscustomobject]@{ name = 'core'; type = 'library'; target = 'core' }
            )

            $result = Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable', 'library', 'shared-library') -Command 'build'
            $result.name | Should Be 'core'
        }
    }

    Context 'Property 5: Multi-target ambiguity error includes all names' {
        # **Validates: Requirements 2.3, 3.3**

        It 'throws error listing all matching target names' {
            $targets = @(
                [pscustomobject]@{ name = 'app1'; type = 'executable'; target = 'app1' },
                [pscustomobject]@{ name = 'app2'; type = 'executable'; target = 'app2' }
            )

            { Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable') -Command 'run' } |
                Should Throw 'app1'
        }

        It 'ambiguity error includes second target name' {
            $targets = @(
                [pscustomobject]@{ name = 'app1'; type = 'executable'; target = 'app1' },
                [pscustomobject]@{ name = 'app2'; type = 'executable'; target = 'app2' }
            )

            { Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable') -Command 'run' } |
                Should Throw 'app2'
        }

        It 'throws error with all three names when three targets match' {
            $targets = @(
                [pscustomobject]@{ name = 'svc1'; type = 'executable'; target = 'svc1' },
                [pscustomobject]@{ name = 'svc2'; type = 'executable'; target = 'svc2' },
                [pscustomobject]@{ name = 'svc3'; type = 'executable'; target = 'svc3' }
            )

            { Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable') -Command 'run' } |
                Should Throw 'svc1, svc2, svc3'
        }
    }

    It 'throws error when no targets of required type exist' {
        $targets = @(
            [pscustomobject]@{ name = 'core'; type = 'library'; target = 'core' },
            [pscustomobject]@{ name = 'utils'; type = 'library'; target = 'utils' }
        )

        { Resolve-CdoDefaultTarget -Targets $targets -AllowedTypes @('executable') -Command 'run' } |
            Should Throw 'No executable'
    }
}

Describe 'Get-CdoTargetByName' {
    Context 'Property 6: Unknown target name error includes available targets' {
        # **Validates: Requirements 2.4**

        It 'throws error listing all available targets when name not found' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'app' },
                [pscustomobject]@{ name = 'core'; type = 'library'; target = 'core' },
                [pscustomobject]@{ name = 'tests'; type = 'test'; target = 'tests' }
            )

            { Get-CdoTargetByName -Targets $targets -Name 'nonexistent' } |
                Should Throw 'app, core, tests'
        }

        It 'error message includes the unknown name' {
            $targets = @(
                [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'app' }
            )

            { Get-CdoTargetByName -Targets $targets -Name 'missing' } |
                Should Throw 'missing'
        }
    }

    It 'finds target by name (case-insensitive)' {
        $targets = @(
            [pscustomobject]@{ name = 'MyApp'; type = 'executable'; target = 'MyApp' }
        )

        $result = Get-CdoTargetByName -Targets $targets -Name 'myapp'
        $result.name | Should Be 'MyApp'
    }

    It 'finds target by target field (case-insensitive)' {
        $targets = @(
            [pscustomobject]@{ name = 'app'; type = 'executable'; target = 'my_app_exe' }
        )

        $result = Get-CdoTargetByName -Targets $targets -Name 'MY_APP_EXE'
        $result.name | Should Be 'app'
        $result.target | Should Be 'my_app_exe'
    }
}
