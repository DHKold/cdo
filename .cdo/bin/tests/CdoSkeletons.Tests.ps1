$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoSkeletons.ps1"

Describe 'C++ Skeleton Definitions' {
    Context 'Property 3: New manifests use targets-only format' {
        foreach ($skeleton in @('cli-cpp', 'sdl3-cpp', 'sdl3-gpu-cpp')) {
            It "skeleton '${skeleton}' manifest has targets array" {
                $def = Get-CdoSkeletonDefinition -Skeleton $skeleton -ProjectName 'test' -ProjectId 'test_proj'
                $def.Manifest.Contains('targets') | Should Be $true
            }

            It "skeleton '${skeleton}' manifest has no apps field" {
                $def = Get-CdoSkeletonDefinition -Skeleton $skeleton -ProjectName 'test' -ProjectId 'test_proj'
                $def.Manifest.Contains('apps') | Should Be $false
            }

            It "skeleton '${skeleton}' manifest has no tests field" {
                $def = Get-CdoSkeletonDefinition -Skeleton $skeleton -ProjectName 'test' -ProjectId 'test_proj'
                $def.Manifest.Contains('tests') | Should Be $false
            }

            It "skeleton '${skeleton}' manifest has cppStandard" {
                $def = Get-CdoSkeletonDefinition -Skeleton $skeleton -ProjectName 'test' -ProjectId 'test_proj'
                $def.Manifest.Contains('cppStandard') | Should Be $true
                $def.Manifest['cppStandard'] | Should Be 17
            }
        }
    }

    Context 'All 7 skeletons are registered' {
        It 'Get-CdoSkeletonNames returns 7 skeletons' {
            $names = Get-CdoSkeletonNames
            $names.Count | Should Be 7
        }

        It 'all skeletons have descriptions' {
            foreach ($name in (Get-CdoSkeletonNames)) {
                $desc = Get-CdoSkeletonDescription -Name $name
                $desc | Should Not BeNullOrEmpty
            }
        }

        It 'C++ skeletons are in the list' {
            $names = Get-CdoSkeletonNames
            ($names -contains 'cli-cpp') | Should Be $true
            ($names -contains 'sdl3-cpp') | Should Be $true
            ($names -contains 'sdl3-gpu-cpp') | Should Be $true
        }
    }

    Context 'C++ skeletons produce .cpp files' {
        It 'cli-cpp skeleton generates .cpp source files' {
            $def = Get-CdoSkeletonDefinition -Skeleton 'cli-cpp' -ProjectName 'test' -ProjectId 'test_proj'
            $paths = @($def.Files | ForEach-Object { $_.Path })
            ($paths -contains 'src\main.cpp') | Should Be $true
            ($paths -contains 'src\app.cpp') | Should Be $true
            ($paths -contains 'tests\test_app.cpp') | Should Be $true
        }

        It 'sdl3-cpp skeleton generates .cpp source files' {
            $def = Get-CdoSkeletonDefinition -Skeleton 'sdl3-cpp' -ProjectName 'test' -ProjectId 'test_proj'
            $paths = @($def.Files | ForEach-Object { $_.Path })
            ($paths -contains 'src\main.cpp') | Should Be $true
            ($paths -contains 'src\app.cpp') | Should Be $true
            ($paths -contains 'tests\test_app.cpp') | Should Be $true
        }

        It 'sdl3-gpu-cpp skeleton generates .cpp source files' {
            $def = Get-CdoSkeletonDefinition -Skeleton 'sdl3-gpu-cpp' -ProjectName 'test' -ProjectId 'test_proj'
            $paths = @($def.Files | ForEach-Object { $_.Path })
            ($paths -contains 'src\main.cpp') | Should Be $true
            ($paths -contains 'src\gpu_app.cpp') | Should Be $true
            ($paths -contains 'src\shader_loader.cpp') | Should Be $true
            ($paths -contains 'tests\test_gpu_app.cpp') | Should Be $true
        }
    }

    Context 'C++ skeletons generate correct CMakeLists.txt' {
        It 'cli-cpp CMakeLists.txt has LANGUAGES C CXX' {
            $def = Get-CdoSkeletonDefinition -Skeleton 'cli-cpp' -ProjectName 'test' -ProjectId 'test_proj'
            $cmake = ($def.Files | Where-Object { $_.Path -eq 'CMakeLists.txt' }).Content
            $cmake | Should Match 'LANGUAGES C CXX'
        }

        It 'sdl3-cpp CMakeLists.txt has LANGUAGES C CXX' {
            $def = Get-CdoSkeletonDefinition -Skeleton 'sdl3-cpp' -ProjectName 'test' -ProjectId 'test_proj'
            $cmake = ($def.Files | Where-Object { $_.Path -eq 'CMakeLists.txt' }).Content
            $cmake | Should Match 'LANGUAGES C CXX'
        }

        It 'sdl3-gpu-cpp CMakeLists.txt has LANGUAGES C CXX' {
            $def = Get-CdoSkeletonDefinition -Skeleton 'sdl3-gpu-cpp' -ProjectName 'test' -ProjectId 'test_proj'
            $cmake = ($def.Files | Where-Object { $_.Path -eq 'CMakeLists.txt' }).Content
            $cmake | Should Match 'LANGUAGES C CXX'
        }
    }

    Context 'Skeleton aliases resolve correctly' {
        It 'cpp resolves to cli-cpp' {
            Resolve-CdoSkeletonName -Name 'cpp' | Should Be 'cli-cpp'
        }

        It 'c++ resolves to cli-cpp' {
            Resolve-CdoSkeletonName -Name 'c++' | Should Be 'cli-cpp'
        }

        It 'gpu-cpp resolves to sdl3-gpu-cpp' {
            Resolve-CdoSkeletonName -Name 'gpu-cpp' | Should Be 'sdl3-gpu-cpp'
        }
    }
}
