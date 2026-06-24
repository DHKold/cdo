$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoTargets.ps1"

Describe 'Get-CdoTargetBuildOrder' {
    Context 'Property 9: Topological ordering of generated CMake targets' {
        # **Validates: Requirements 8.2, 8.3**
        # For any valid (acyclic) dependency graph among targets, the generated build order
        # SHALL define every dependency target before the target that depends on it.
        # For all targets A and B where A depends on B, B SHALL appear before A in the sorted output.

        It 'single target with no dependencies returns that target' {
            $targets = @(
                @{ name = 'app'; type = 'executable'; dependencies = @() }
            )

            $result = @(Get-CdoTargetBuildOrder -Targets $targets)
            @($result).Count | Should Be 1
            @($result)[0].name | Should Be 'app'
        }

        It 'linear chain A -> B -> C orders C before B before A' {
            $targets = @(
                @{ name = 'app'; type = 'executable'; dependencies = @('middleware') },
                @{ name = 'middleware'; type = 'library'; linkage = 'static'; dependencies = @('core') },
                @{ name = 'core'; type = 'library'; linkage = 'static'; dependencies = @() }
            )

            $result = Get-CdoTargetBuildOrder -Targets $targets
            $names = @($result | ForEach-Object { $_.name })
            $names.IndexOf('core') | Should BeLessThan $names.IndexOf('middleware')
            $names.IndexOf('middleware') | Should BeLessThan $names.IndexOf('app')
        }

        It 'diamond dependency: app -> (renderer, engine), renderer -> engine' {
            $targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; dependencies = @() },
                @{ name = 'renderer'; type = 'library'; linkage = 'shared'; dependencies = @('engine') },
                @{ name = 'app'; type = 'executable'; dependencies = @('engine', 'renderer') }
            )

            $result = Get-CdoTargetBuildOrder -Targets $targets
            $names = @($result | ForEach-Object { $_.name })

            # engine must come before renderer
            $names.IndexOf('engine') | Should BeLessThan $names.IndexOf('renderer')
            # engine must come before app
            $names.IndexOf('engine') | Should BeLessThan $names.IndexOf('app')
            # renderer must come before app
            $names.IndexOf('renderer') | Should BeLessThan $names.IndexOf('app')
        }

        It 'independent targets with no dependencies can appear in any order but all present' {
            $targets = @(
                @{ name = 'alpha'; type = 'executable'; dependencies = @() },
                @{ name = 'beta'; type = 'library'; linkage = 'static'; dependencies = @() },
                @{ name = 'gamma'; type = 'library'; linkage = 'shared'; dependencies = @() }
            )

            $result = Get-CdoTargetBuildOrder -Targets $targets
            $names = @($result | ForEach-Object { $_.name })
            $names.Count | Should Be 3
            ($names -contains 'alpha') | Should Be $true
            ($names -contains 'beta') | Should Be $true
            ($names -contains 'gamma') | Should Be $true
        }

        It 'multiple dependencies: app depends on audio, renderer, and engine' {
            $targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; dependencies = @() },
                @{ name = 'audio'; type = 'library'; linkage = 'static'; dependencies = @('engine') },
                @{ name = 'renderer'; type = 'library'; linkage = 'shared'; dependencies = @('engine') },
                @{ name = 'app'; type = 'executable'; dependencies = @('audio', 'renderer', 'engine') }
            )

            $result = Get-CdoTargetBuildOrder -Targets $targets
            $names = @($result | ForEach-Object { $_.name })

            # For every edge (A depends on B), indexOf(B) < indexOf(A)
            $names.IndexOf('engine') | Should BeLessThan $names.IndexOf('audio')
            $names.IndexOf('engine') | Should BeLessThan $names.IndexOf('renderer')
            $names.IndexOf('engine') | Should BeLessThan $names.IndexOf('app')
            $names.IndexOf('audio') | Should BeLessThan $names.IndexOf('app')
            $names.IndexOf('renderer') | Should BeLessThan $names.IndexOf('app')
        }

        It 'deep chain of 5 targets preserves full ordering' {
            $targets = @(
                @{ name = 'a'; type = 'executable'; dependencies = @('b') },
                @{ name = 'b'; type = 'library'; linkage = 'static'; dependencies = @('c') },
                @{ name = 'c'; type = 'library'; linkage = 'static'; dependencies = @('d') },
                @{ name = 'd'; type = 'library'; linkage = 'static'; dependencies = @('e') },
                @{ name = 'e'; type = 'library'; linkage = 'static'; dependencies = @() }
            )

            $result = Get-CdoTargetBuildOrder -Targets $targets
            $names = @($result | ForEach-Object { $_.name })

            # Verify full chain: e < d < c < b < a
            $names.IndexOf('e') | Should BeLessThan $names.IndexOf('d')
            $names.IndexOf('d') | Should BeLessThan $names.IndexOf('c')
            $names.IndexOf('c') | Should BeLessThan $names.IndexOf('b')
            $names.IndexOf('b') | Should BeLessThan $names.IndexOf('a')
        }

        It 'result contains all original target objects' {
            $targets = @(
                @{ name = 'core'; type = 'library'; linkage = 'static'; dependencies = @() },
                @{ name = 'app'; type = 'executable'; dependencies = @('core') }
            )

            $result = Get-CdoTargetBuildOrder -Targets $targets
            $result.Count | Should Be 2
            $resultNames = @($result | ForEach-Object { $_.name })
            ($resultNames -contains 'core') | Should Be $true
            ($resultNames -contains 'app') | Should Be $true
        }

        It 'wide fan-in: multiple libraries with shared base dependency' {
            $targets = @(
                @{ name = 'base'; type = 'library'; linkage = 'static'; dependencies = @() },
                @{ name = 'net'; type = 'library'; linkage = 'static'; dependencies = @('base') },
                @{ name = 'gfx'; type = 'library'; linkage = 'shared'; dependencies = @('base') },
                @{ name = 'audio'; type = 'library'; linkage = 'static'; dependencies = @('base') },
                @{ name = 'game'; type = 'executable'; dependencies = @('net', 'gfx', 'audio') }
            )

            $result = Get-CdoTargetBuildOrder -Targets $targets
            $names = @($result | ForEach-Object { $_.name })

            # base must come before all its dependents
            $names.IndexOf('base') | Should BeLessThan $names.IndexOf('net')
            $names.IndexOf('base') | Should BeLessThan $names.IndexOf('gfx')
            $names.IndexOf('base') | Should BeLessThan $names.IndexOf('audio')
            # net, gfx, audio must come before game
            $names.IndexOf('net') | Should BeLessThan $names.IndexOf('game')
            $names.IndexOf('gfx') | Should BeLessThan $names.IndexOf('game')
            $names.IndexOf('audio') | Should BeLessThan $names.IndexOf('game')
        }
    }

    Context 'Property 10: Circular dependency detection' {
        # **Validates: Requirements 8.5**
        # For any set of targets whose dependency declarations form a cycle,
        # Get-CdoTargetBuildOrder SHALL throw an error identifying the targets involved
        # in the cycle rather than producing output.

        It 'detects simple 2-node cycle: A -> B -> A' {
            $targets = @(
                @{ name = 'alpha'; type = 'library'; linkage = 'static'; dependencies = @('beta') },
                @{ name = 'beta'; type = 'library'; linkage = 'static'; dependencies = @('alpha') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'Circular dependency'
        }

        It '2-node cycle error mentions both target names' {
            $targets = @(
                @{ name = 'alpha'; type = 'library'; linkage = 'static'; dependencies = @('beta') },
                @{ name = 'beta'; type = 'library'; linkage = 'static'; dependencies = @('alpha') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'alpha'
            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'beta'
        }

        It 'detects 3-node cycle: A -> B -> C -> A' {
            $targets = @(
                @{ name = 'x'; type = 'library'; linkage = 'static'; dependencies = @('y') },
                @{ name = 'y'; type = 'library'; linkage = 'static'; dependencies = @('z') },
                @{ name = 'z'; type = 'library'; linkage = 'static'; dependencies = @('x') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'Circular dependency'
        }

        It '3-node cycle error mentions all involved targets' {
            $targets = @(
                @{ name = 'x'; type = 'library'; linkage = 'static'; dependencies = @('y') },
                @{ name = 'y'; type = 'library'; linkage = 'static'; dependencies = @('z') },
                @{ name = 'z'; type = 'library'; linkage = 'static'; dependencies = @('x') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'x'
            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'y'
            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'z'
        }

        It 'detects cycle even with non-cyclic targets present' {
            $targets = @(
                @{ name = 'base'; type = 'library'; linkage = 'static'; dependencies = @() },
                @{ name = 'cycleA'; type = 'library'; linkage = 'static'; dependencies = @('cycleB') },
                @{ name = 'cycleB'; type = 'library'; linkage = 'static'; dependencies = @('cycleA') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'Circular dependency'
        }

        It 'cycle with non-cyclic targets only mentions cyclic targets in error' {
            $targets = @(
                @{ name = 'base'; type = 'library'; linkage = 'static'; dependencies = @() },
                @{ name = 'cycleA'; type = 'library'; linkage = 'static'; dependencies = @('cycleB') },
                @{ name = 'cycleB'; type = 'library'; linkage = 'static'; dependencies = @('cycleA') }
            )

            try {
                Get-CdoTargetBuildOrder -Targets $targets
                throw 'Should have thrown'
            } catch {
                $_.Exception.Message | Should Match 'cycleA'
                $_.Exception.Message | Should Match 'cycleB'
                $_.Exception.Message | Should Not Match 'base'
            }
        }

        It 'detects self-dependency as a cycle' {
            $targets = @(
                @{ name = 'selfdep'; type = 'library'; linkage = 'static'; dependencies = @('selfdep') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'Circular dependency'
        }

        It 'detects 4-node cycle: A -> B -> C -> D -> A' {
            $targets = @(
                @{ name = 'w'; type = 'library'; linkage = 'static'; dependencies = @('x') },
                @{ name = 'x'; type = 'library'; linkage = 'static'; dependencies = @('y') },
                @{ name = 'y'; type = 'library'; linkage = 'static'; dependencies = @('z') },
                @{ name = 'z'; type = 'library'; linkage = 'static'; dependencies = @('w') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'Circular dependency'
        }
    }

    Context 'Property 11: Missing dependency detection' {
        # **Validates: Requirements 8.4**
        # For any target whose dependencies array references a name not present in the
        # targets list, Get-CdoTargetBuildOrder SHALL throw an error identifying the
        # missing dependency name.

        It 'detects reference to non-existent target' {
            $targets = @(
                @{ name = 'app'; type = 'executable'; dependencies = @('nonexistent') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'nonexistent'
        }

        It 'error message includes the dependent target name' {
            $targets = @(
                @{ name = 'myapp'; type = 'executable'; dependencies = @('missing_lib') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'myapp'
        }

        It 'error message includes the missing dependency name' {
            $targets = @(
                @{ name = 'myapp'; type = 'executable'; dependencies = @('missing_lib') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'missing_lib'
        }

        It 'detects missing dependency even when other valid dependencies exist' {
            $targets = @(
                @{ name = 'core'; type = 'library'; linkage = 'static'; dependencies = @() },
                @{ name = 'app'; type = 'executable'; dependencies = @('core', 'phantom') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'phantom'
        }

        It 'detects missing dependency in a library target' {
            $targets = @(
                @{ name = 'renderer'; type = 'library'; linkage = 'shared'; dependencies = @('graphics_api') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'graphics_api'
        }

        It 'error mentions does not exist phrasing' {
            $targets = @(
                @{ name = 'app'; type = 'executable'; dependencies = @('ghost') }
            )

            { Get-CdoTargetBuildOrder -Targets $targets } | Should Throw 'does not exist'
        }
    }
}
