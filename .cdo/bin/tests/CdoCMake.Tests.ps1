$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoSkeletons.ps1"

Describe 'Get-CdoMultiTargetCMakeContent' {
    Context 'Property 8: C++ CMake directives present when cppStandard set' {
        # **Validates: Requirements 5.2, 5.3**

        It 'includes LANGUAGES C CXX when cppStandard is set' {
            $config = @{ cStandard = 17; cppStandard = 20 }
            $targets = @(@{ name = 'app'; type = 'executable'; target = 'app'; sources = @('src/main.cpp') })
            $result = Get-CdoMultiTargetCMakeContent -ProjectId 'test' -Targets $targets -Config $config
            $result | Should Match 'LANGUAGES C CXX'
        }

        It 'includes CMAKE_CXX_STANDARD with correct value' {
            $config = @{ cStandard = 17; cppStandard = 20 }
            $targets = @(@{ name = 'app'; type = 'executable'; target = 'app'; sources = @('src/main.cpp') })
            $result = Get-CdoMultiTargetCMakeContent -ProjectId 'test' -Targets $targets -Config $config
            $result | Should Match 'set\(CMAKE_CXX_STANDARD 20\)'
            $result | Should Match 'set\(CMAKE_CXX_STANDARD_REQUIRED ON\)'
            $result | Should Match 'set\(CMAKE_CXX_EXTENSIONS OFF\)'
        }
    }

    Context 'Property 9: C++ CMake directives absent when cppStandard not set' {
        # **Validates: Requirements 5.4**

        It 'uses LANGUAGES C only when no cppStandard' {
            $config = @{ cStandard = 17 }
            $targets = @(@{ name = 'app'; type = 'executable'; target = 'app'; sources = @('src/main.c') })
            $result = Get-CdoMultiTargetCMakeContent -ProjectId 'test' -Targets $targets -Config $config
            $result | Should Match 'LANGUAGES C\)'
            $result | Should Not Match 'LANGUAGES C CXX'
            $result | Should Not Match 'CMAKE_CXX_STANDARD'
        }
    }

    Context 'Property 12: CMake generation target-type directive count' {
        # **Validates: Requirements 8.1, 8.2, 8.3**

        It 'emits correct number of add_executable directives' {
            $config = @{ cStandard = 17 }
            $targets = @(
                @{ name = 'app1'; type = 'executable'; target = 'app1'; sources = @('src/a.c') },
                @{ name = 'app2'; type = 'executable'; target = 'app2'; sources = @('src/b.c') },
                @{ name = 'core'; type = 'library'; target = 'core'; sources = @('src/c.c') }
            )
            $result = Get-CdoMultiTargetCMakeContent -ProjectId 'test' -Targets $targets -Config $config
            $exeCount = ([regex]::Matches($result, 'add_executable\(')).Count
            $exeCount | Should Be 2
        }

        It 'emits correct number of add_library STATIC directives' {
            $config = @{ cStandard = 17 }
            $targets = @(
                @{ name = 'core'; type = 'library'; target = 'core'; sources = @('src/c.c') },
                @{ name = 'utils'; type = 'library'; target = 'utils'; sources = @('src/u.c') }
            )
            $result = Get-CdoMultiTargetCMakeContent -ProjectId 'test' -Targets $targets -Config $config
            $staticCount = ([regex]::Matches($result, 'add_library\(\S+ STATIC')).Count
            $staticCount | Should Be 2
        }

        It 'emits correct number of add_library SHARED directives' {
            $config = @{ cStandard = 17 }
            $targets = @(
                @{ name = 'mylib'; type = 'shared-library'; target = 'mylib'; sources = @('src/lib.c') }
            )
            $result = Get-CdoMultiTargetCMakeContent -ProjectId 'test' -Targets $targets -Config $config
            $sharedCount = ([regex]::Matches($result, 'add_library\(\S+ SHARED')).Count
            $sharedCount | Should Be 1
        }
    }

    Context 'Property 13: Test targets are wrapped and registered' {
        # **Validates: Requirements 8.4**

        It 'wraps test targets in if(CDO_ENABLE_TESTS)' {
            $config = @{ cStandard = 17 }
            $targets = @(
                @{ name = 'app'; type = 'executable'; target = 'app'; sources = @('src/main.c') },
                @{ name = 'tests'; type = 'test'; target = 'app_tests'; sources = @('tests/test.c') }
            )
            $result = Get-CdoMultiTargetCMakeContent -ProjectId 'test' -Targets $targets -Config $config
            $result | Should Match 'if\(CDO_ENABLE_TESTS\)'
            $result | Should Match 'enable_testing\(\)'
            $result | Should Match 'add_test\(NAME app_tests COMMAND app_tests\)'
        }
    }

    Context 'Property 14: Dependency macros applied to every target' {
        # **Validates: Requirements 8.5**

        It 'applies cdo_apply_dependencies to every target' {
            $config = @{ cStandard = 17 }
            $targets = @(
                @{ name = 'app'; type = 'executable'; target = 'app'; sources = @('src/main.c') },
                @{ name = 'core'; type = 'library'; target = 'core'; sources = @('src/lib.c') },
                @{ name = 'tests'; type = 'test'; target = 'app_tests'; sources = @('tests/t.c') }
            )
            $result = Get-CdoMultiTargetCMakeContent -ProjectId 'test' -Targets $targets -Config $config
            $applyCount = ([regex]::Matches($result, 'cdo_apply_dependencies\(')).Count
            $applyCount | Should Be 3
        }

        It 'applies cdo_copy_runtime_dependencies to every target' {
            $config = @{ cStandard = 17 }
            $targets = @(
                @{ name = 'app'; type = 'executable'; target = 'app'; sources = @('src/main.c') },
                @{ name = 'tests'; type = 'test'; target = 'app_tests'; sources = @('tests/t.c') }
            )
            $result = Get-CdoMultiTargetCMakeContent -ProjectId 'test' -Targets $targets -Config $config
            $copyCount = ([regex]::Matches($result, 'cdo_copy_runtime_dependencies\(')).Count
            $copyCount | Should Be 2
        }
    }
}
