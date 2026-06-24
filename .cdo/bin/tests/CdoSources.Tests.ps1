$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoSources.ps1"

Describe 'Source Discovery C++ Extensions' {
    Context 'Property 10: Source discovery accepts all C/C++ extensions' {
        # **Validates: Requirements 6.1, 6.3**

        It 'Get-CdoSourcePathFromCMakeLine recognizes .c files' {
            Get-CdoSourcePathFromCMakeLine -Line '    src/main.c' | Should Be 'src/main.c'
        }

        It 'Get-CdoSourcePathFromCMakeLine recognizes .cpp files' {
            Get-CdoSourcePathFromCMakeLine -Line '    src/main.cpp' | Should Be 'src/main.cpp'
        }

        It 'Get-CdoSourcePathFromCMakeLine recognizes .cc files' {
            Get-CdoSourcePathFromCMakeLine -Line '    src/app.cc' | Should Be 'src/app.cc'
        }

        It 'Get-CdoSourcePathFromCMakeLine recognizes .cxx files' {
            Get-CdoSourcePathFromCMakeLine -Line '    src/util.cxx' | Should Be 'src/util.cxx'
        }

        It 'Get-CdoSourcePathFromCMakeLine rejects non-C/C++ files' {
            Get-CdoSourcePathFromCMakeLine -Line '    src/notes.txt' | Should BeNullOrEmpty
            Get-CdoSourcePathFromCMakeLine -Line '    src/header.h' | Should BeNullOrEmpty
        }
    }

    Context 'Property 11: CMake source block parser recognizes all C/C++ extensions' {
        # **Validates: Requirements 6.2**

        It 'parses add_executable block with mixed C/C++ sources' {
            $content = @"
add_executable(app
    src/main.cpp
    src/util.c
    src/helper.cc
    src/engine.cxx
)
"@
            $blocks = @(Get-CdoCMakeSourceBlocks -Content $content)
            $blocks.Count | Should Be 1
            $blocks[0].Sources.Count | Should Be 4
            ($blocks[0].Sources -contains 'src/main.cpp') | Should Be $true
            ($blocks[0].Sources -contains 'src/util.c') | Should Be $true
            ($blocks[0].Sources -contains 'src/helper.cc') | Should Be $true
            ($blocks[0].Sources -contains 'src/engine.cxx') | Should Be $true
        }
    }

    Context 'Source selection validates C/C++ extensions' {
        # **Validates: Requirements 6.1, 6.3**

        BeforeAll {
            $script:testRoot = Join-Path $TestDrive 'project'
            New-Item -ItemType Directory -Path (Join-Path $script:testRoot 'src') -Force | Out-Null
            'int main(){}' | Set-Content (Join-Path $script:testRoot 'src/main.cpp')
            'void f(){}' | Set-Content (Join-Path $script:testRoot 'src/util.cc')
            'void g(){}' | Set-Content (Join-Path $script:testRoot 'src/lib.cxx')
            'void h(){}' | Set-Content (Join-Path $script:testRoot 'src/old.c')
        }

        It 'accepts .cpp files in source selection' {
            { Resolve-CdoSourceSelection -Root $script:testRoot -Files @('src/main.cpp') } | Should Not Throw
        }

        It 'accepts .cc files in source selection' {
            { Resolve-CdoSourceSelection -Root $script:testRoot -Files @('src/util.cc') } | Should Not Throw
        }

        It 'accepts .cxx files in source selection' {
            { Resolve-CdoSourceSelection -Root $script:testRoot -Files @('src/lib.cxx') } | Should Not Throw
        }

        It 'accepts .c files in source selection' {
            { Resolve-CdoSourceSelection -Root $script:testRoot -Files @('src/old.c') } | Should Not Throw
        }

        It 'rejects .txt files in source selection' {
            'hello' | Set-Content (Join-Path $script:testRoot 'src/notes.txt')
            { Resolve-CdoSourceSelection -Root $script:testRoot -Files @('src/notes.txt') } | Should Throw 'C/C++'
        }
    }
}
