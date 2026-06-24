$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoTargets.ps1"

Describe 'Property 5: CMake generator emits correct library type keyword' {
    # **Validates: Requirements 3.7, 3.8**
    # For any library target in the manifest, the generated CMakeLists.txt SHALL
    # contain add_library(<target> STATIC ...) if linkage is static, or
    # add_library(<target> SHARED ...) if linkage is shared.
    # No library SHALL be emitted without an explicit type keyword.

    BeforeEach {
        $script:tempDir = New-Item -ItemType Directory -Path (Join-Path $TestDrive ([System.Guid]::NewGuid().ToString()))
        # Create minimal source files so the generator can discover them
    }

    It 'static library emits add_library with STATIC keyword' {
        $srcDir = Join-Path $tempDir 'src\engine'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'engine.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'add_library\(engine STATIC'
    }

    It 'shared library emits add_library with SHARED keyword' {
        $srcDir = Join-Path $tempDir 'src\renderer'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'renderer.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'renderer'; type = 'library'; linkage = 'shared'; target = 'renderer'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'add_library\(renderer SHARED'
    }

    It 'multiple libraries each get their correct linkage keyword' {
        $srcEngine = Join-Path $tempDir 'src\engine'
        $srcRenderer = Join-Path $tempDir 'src\renderer'
        New-Item -ItemType Directory -Path $srcEngine -Force | Out-Null
        New-Item -ItemType Directory -Path $srcRenderer -Force | Out-Null
        Set-Content -Path (Join-Path $srcEngine 'engine.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcRenderer 'renderer.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                @{ name = 'renderer'; type = 'library'; linkage = 'shared'; target = 'renderer'; dependencies = @('engine') }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'add_library\(engine STATIC'
        $cmake | Should Match 'add_library\(renderer SHARED'
    }

    It 'no library is emitted without STATIC or SHARED keyword' {
        $srcDir = Join-Path $tempDir 'src\mylib'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'mylib.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'mylib'; type = 'library'; linkage = 'static'; target = 'mylib'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        # Ensure add_library always has a STATIC or SHARED keyword after the name
        $cmake | Should Not Match 'add_library\(mylib\s+\n'
        $cmake | Should Match 'add_library\(mylib (STATIC|SHARED)'
    }

    It 'executable targets use add_executable not add_library' {
        $srcDir = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'add_executable\(app'
        $cmake | Should Not Match 'add_library\(app'
    }
}

Describe 'Property 6: CMake generator references correct per-target source paths' {
    # **Validates: Requirements 6.3, 6.4, 6.5, 7.3**
    # For any target in the manifest, the generated CMakeLists.txt SHALL reference
    # source files only from src/<target_name>/ for executables and libraries,
    # from include/<target_name>/ for library include directories, and from
    # tests/<target_name>/ for test targets.

    BeforeEach {
        $script:tempDir = New-Item -ItemType Directory -Path (Join-Path $TestDrive ([System.Guid]::NewGuid().ToString()))
    }

    It 'executable target references sources from src/<name>/' {
        $srcDir = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'main.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcDir 'utils.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'src/app/main\.cpp'
        $cmake | Should Match 'src/app/utils\.cpp'
    }

    It 'library target references sources from src/<name>/' {
        $srcDir = Join-Path $tempDir 'src\engine'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'engine.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'src/engine/engine\.cpp'
    }

    It 'library target includes directory from include/<name>/' {
        $srcDir = Join-Path $tempDir 'src\engine'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'engine.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'target_include_directories\(engine'
        $cmake | Should Match 'include/engine'
    }

    It 'test target references sources from tests/<target_name>/' {
        $srcDir = Join-Path $tempDir 'src\engine'
        $testDir = Join-Path $tempDir 'tests\engine'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        New-Item -ItemType Directory -Path $testDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'engine.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $testDir 'test_engine.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                @{ name = 'engine_tests'; type = 'test'; target = 'engine_tests'; testFor = 'engine' }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'tests/engine/test_engine\.cpp'
    }

    It 'executable does not reference include directory' {
        $srcDir = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Not Match 'target_include_directories\(app'
    }

    It 'multiple targets each reference only their own source directory' {
        $srcEngine = Join-Path $tempDir 'src\engine'
        $srcApp = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcEngine -Force | Out-Null
        New-Item -ItemType Directory -Path $srcApp -Force | Out-Null
        Set-Content -Path (Join-Path $srcEngine 'engine.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcApp 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @('engine') }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        # Engine section references src/engine/ not src/app/
        $cmake | Should Match 'src/engine/engine\.cpp'
        # App section references src/app/ not src/engine/
        $cmake | Should Match 'src/app/main\.cpp'
    }
}

Describe 'Property 12: Shared library DLL copy commands for dependents' {
    # **Validates: Requirements 9.1, 9.3**
    # For any executable or test target that transitively depends on a shared
    # library target, the generated CMakeLists.txt SHALL contain a post-build
    # command copying that shared library's DLL to the dependent's output directory.

    BeforeEach {
        $script:tempDir = New-Item -ItemType Directory -Path (Join-Path $TestDrive ([System.Guid]::NewGuid().ToString()))
    }

    It 'executable depending on shared library gets DLL copy command' {
        $srcEngine = Join-Path $tempDir 'src\engine'
        $srcApp = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcEngine -Force | Out-Null
        New-Item -ItemType Directory -Path $srcApp -Force | Out-Null
        Set-Content -Path (Join-Path $srcEngine 'engine.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcApp 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'shared'; target = 'engine'; dependencies = @() },
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @('engine') }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'add_custom_command\(TARGET app POST_BUILD'
        $cmake | Should Match 'copy_if_different'
        $cmake | Should Match 'TARGET_FILE:engine'
        $cmake | Should Match 'TARGET_FILE_DIR:app'
    }

    It 'executable depending only on static library does NOT get DLL copy command' {
        $srcEngine = Join-Path $tempDir 'src\engine'
        $srcApp = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcEngine -Force | Out-Null
        New-Item -ItemType Directory -Path $srcApp -Force | Out-Null
        Set-Content -Path (Join-Path $srcEngine 'engine.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcApp 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @('engine') }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Not Match 'add_custom_command\(TARGET app POST_BUILD'
    }

    It 'transitive shared library dependency generates DLL copy' {
        $srcCore = Join-Path $tempDir 'src\core'
        $srcRenderer = Join-Path $tempDir 'src\renderer'
        $srcApp = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcCore -Force | Out-Null
        New-Item -ItemType Directory -Path $srcRenderer -Force | Out-Null
        New-Item -ItemType Directory -Path $srcApp -Force | Out-Null
        Set-Content -Path (Join-Path $srcCore 'core.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcRenderer 'renderer.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcApp 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'core'; type = 'library'; linkage = 'static'; target = 'core'; dependencies = @() },
                @{ name = 'renderer'; type = 'library'; linkage = 'shared'; target = 'renderer'; dependencies = @('core') },
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @('renderer') }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        # app transitively depends on renderer (shared), so DLL copy should exist
        $cmake | Should Match 'add_custom_command\(TARGET app POST_BUILD'
        $cmake | Should Match 'TARGET_FILE:renderer'
    }

    It 'test target depending on shared library gets DLL copy command' {
        $srcRenderer = Join-Path $tempDir 'src\renderer'
        $testDir = Join-Path $tempDir 'tests\renderer'
        New-Item -ItemType Directory -Path $srcRenderer -Force | Out-Null
        New-Item -ItemType Directory -Path $testDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcRenderer 'renderer.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $testDir 'test_renderer.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'renderer'; type = 'library'; linkage = 'shared'; target = 'renderer'; dependencies = @() },
                @{ name = 'renderer_tests'; type = 'test'; target = 'renderer_tests'; testFor = 'renderer' }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'add_custom_command\(TARGET renderer_tests POST_BUILD'
        $cmake | Should Match 'TARGET_FILE:renderer'
        $cmake | Should Match 'TARGET_FILE_DIR:renderer_tests'
    }

    It 'multiple shared libraries produce multiple DLL copy commands' {
        $srcAudio = Join-Path $tempDir 'src\audio'
        $srcGfx = Join-Path $tempDir 'src\gfx'
        $srcApp = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcAudio -Force | Out-Null
        New-Item -ItemType Directory -Path $srcGfx -Force | Out-Null
        New-Item -ItemType Directory -Path $srcApp -Force | Out-Null
        Set-Content -Path (Join-Path $srcAudio 'audio.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcGfx 'gfx.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcApp 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'audio'; type = 'library'; linkage = 'shared'; target = 'audio'; dependencies = @() },
                @{ name = 'gfx'; type = 'library'; linkage = 'shared'; target = 'gfx'; dependencies = @() },
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @('audio', 'gfx') }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'TARGET_FILE:audio'
        $cmake | Should Match 'TARGET_FILE:gfx'
    }
}

Describe 'Property 13: External dependency integration is always preserved' {
    # **Validates: Requirements 10.1, 10.3**
    # For any generated CMakeLists.txt regardless of target configuration, the
    # output SHALL contain the include(cmake/cdo_deps.cmake OPTIONAL) directive,
    # the cdo_apply_dependencies stub guard, and a cdo_apply_dependencies(<target>)
    # call for every target.

    BeforeEach {
        $script:tempDir = New-Item -ItemType Directory -Path (Join-Path $TestDrive ([System.Guid]::NewGuid().ToString()))
    }

    It 'includes cdo_deps.cmake OPTIONAL directive' {
        $srcDir = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'include\(cmake/cdo_deps\.cmake OPTIONAL\)'
    }

    It 'contains cdo_apply_dependencies stub guard' {
        $srcDir = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcDir 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @() }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'if\(NOT COMMAND cdo_apply_dependencies\)'
        $cmake | Should Match 'function\(cdo_apply_dependencies target\)'
    }

    It 'emits cdo_apply_dependencies for every non-test target' {
        $srcEngine = Join-Path $tempDir 'src\engine'
        $srcApp = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcEngine -Force | Out-Null
        New-Item -ItemType Directory -Path $srcApp -Force | Out-Null
        Set-Content -Path (Join-Path $srcEngine 'engine.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcApp 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @('engine') }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'cdo_apply_dependencies\(engine\)'
        $cmake | Should Match 'cdo_apply_dependencies\(app\)'
    }

    It 'emits cdo_apply_dependencies for test targets too' {
        $srcEngine = Join-Path $tempDir 'src\engine'
        $testDir = Join-Path $tempDir 'tests\engine'
        New-Item -ItemType Directory -Path $srcEngine -Force | Out-Null
        New-Item -ItemType Directory -Path $testDir -Force | Out-Null
        Set-Content -Path (Join-Path $srcEngine 'engine.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $testDir 'test_engine.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'engine'; type = 'library'; linkage = 'static'; target = 'engine'; dependencies = @() },
                @{ name = 'engine_tests'; type = 'test'; target = 'engine_tests'; testFor = 'engine' }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'cdo_apply_dependencies\(engine\)'
        $cmake | Should Match 'cdo_apply_dependencies\(engine_tests\)'
    }

    It 'preserves external dependency integration with zero targets' {
        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @()
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        $cmake | Should Match 'include\(cmake/cdo_deps\.cmake OPTIONAL\)'
        $cmake | Should Match 'if\(NOT COMMAND cdo_apply_dependencies\)'
    }

    It 'preserves integration with complex multi-target configuration' {
        $srcCore = Join-Path $tempDir 'src\core'
        $srcRenderer = Join-Path $tempDir 'src\renderer'
        $srcApp = Join-Path $tempDir 'src\app'
        New-Item -ItemType Directory -Path $srcCore -Force | Out-Null
        New-Item -ItemType Directory -Path $srcRenderer -Force | Out-Null
        New-Item -ItemType Directory -Path $srcApp -Force | Out-Null
        Set-Content -Path (Join-Path $srcCore 'core.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcRenderer 'renderer.cpp') -Value '// stub'
        Set-Content -Path (Join-Path $srcApp 'main.cpp') -Value '// stub'

        $config = @{
            name = 'testproj'
            version = '1.0.0'
            cStandard = 17
            cppStandard = 20
            targets = @(
                @{ name = 'core'; type = 'library'; linkage = 'static'; target = 'core'; dependencies = @() },
                @{ name = 'renderer'; type = 'library'; linkage = 'shared'; target = 'renderer'; dependencies = @('core') },
                @{ name = 'app'; type = 'executable'; target = 'app'; dependencies = @('core', 'renderer') }
            )
        }

        Write-CdoCMakeLists -Root $tempDir.FullName -Config $config
        $cmake = Get-Content (Join-Path $tempDir.FullName 'CMakeLists.txt') -Raw

        # All structural elements present
        $cmake | Should Match 'include\(cmake/cdo_deps\.cmake OPTIONAL\)'
        $cmake | Should Match 'if\(NOT COMMAND cdo_apply_dependencies\)'
        # Every target gets cdo_apply_dependencies
        $cmake | Should Match 'cdo_apply_dependencies\(core\)'
        $cmake | Should Match 'cdo_apply_dependencies\(renderer\)'
        $cmake | Should Match 'cdo_apply_dependencies\(app\)'
    }
}
