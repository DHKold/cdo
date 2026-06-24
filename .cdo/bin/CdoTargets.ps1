<#
Target management functions for CDo.
Provides target name validation, manifest target helpers, and
CRUD operations for the targets array in project.cdo.json.
#>

function Test-CdoTargetName {
    param([string]$Name)

    # Valid: lowercase letters, digits, underscores, hyphens
    # Must start with a letter
    if ($Name -cnotmatch '^[a-z][a-z0-9_-]*$') { return $false }

    # Must not collide with reserved names
    $reserved = @('all', 'clean', 'test', 'install', 'package')
    if ($Name -in $reserved) { return $false }

    return $true
}

function Get-CdoTargets {
    param([string]$Root)

    $manifestPath = Join-Path $Root 'project.cdo.json'
    $config = Read-CdoJson -Path $manifestPath
    if ($null -eq $config) {
        throw "No project.cdo.json found at '$Root'. Run 'cdo init' first."
    }

    $targets = Get-CdoProperty -Object $config -Name 'targets' -Default @()
    return @($targets)
}

function Get-CdoTargetByName {
    param(
        [object[]]$Targets,
        [string]$Name
    )

    $found = $Targets | Where-Object {
        $tName = Get-CdoProperty -Object $_ -Name 'name'
        $tName -eq $Name
    }

    if ($null -ne $found) {
        if ($found -is [array]) { return $found[0] }
        return $found
    }

    return $null
}

function Test-CdoTargetExists {
    param(
        [object[]]$Targets,
        [string]$Name
    )

    $existing = Get-CdoTargetByName -Targets $Targets -Name $Name
    return ($null -ne $existing)
}

function Get-CdoSharedLibDependencies {
    param(
        [object]$Target,
        [object[]]$AllTargets
    )

    $visited = @{}
    $sharedLibs = [System.Collections.ArrayList]::new()

    # Inner function to walk the transitive dependency closure
    $walkScript = {
        param([string]$Name)

        if ($visited.ContainsKey($Name)) { return }
        $visited[$Name] = $true

        $t = Get-CdoTargetByName -Targets $AllTargets -Name $Name
        if ($null -eq $t) { return }

        $tType = Get-CdoProperty -Object $t -Name 'type'
        $tLinkage = Get-CdoProperty -Object $t -Name 'linkage'

        if ($tType -eq 'library' -and $tLinkage -eq 'shared') {
            [void]$sharedLibs.Add($t)
        }

        $deps = @(Get-CdoProperty -Object $t -Name 'dependencies' -Default @() | Where-Object { $_ })
        foreach ($dep in $deps) {
            & $walkScript -Name $dep
        }
    }

    # Walk each direct dependency of the target
    $targetDeps = @(Get-CdoProperty -Object $Target -Name 'dependencies' -Default @() | Where-Object { $_ })
    foreach ($dep in $targetDeps) {
        & $walkScript -Name $dep
    }

    return @($sharedLibs)
}

function Get-CdoTargetBuildOrder {
    param([object[]]$Targets)

    # Build adjacency and in-degree maps
    $inDegree = @{}
    $adjacency = @{}
    $targetMap = @{}

    foreach ($t in $Targets) {
        $name = Get-CdoProperty -Object $t -Name 'name'
        $targetMap[$name] = $t
        if (-not $inDegree.ContainsKey($name)) { $inDegree[$name] = 0 }
        if (-not $adjacency.ContainsKey($name)) { $adjacency[$name] = @() }

        $deps = @(Get-CdoProperty -Object $t -Name 'dependencies' -Default @() | Where-Object { $_ })
        foreach ($dep in $deps) {
            # Validate dependency exists
            $depExists = $Targets | Where-Object { (Get-CdoProperty -Object $_ -Name 'name') -eq $dep }
            if (-not $depExists) {
                throw "Target '$name' depends on '$dep' which does not exist."
            }
            $adjacency[$dep] += $name
            $inDegree[$name]++
        }
    }

    # Kahn's algorithm
    $queue = [System.Collections.Queue]::new()
    foreach ($name in @($inDegree.Keys)) {
        if ($inDegree[$name] -eq 0) { $queue.Enqueue($name) }
    }

    $sorted = @()
    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        $sorted += $current
        foreach ($neighbor in $adjacency[$current]) {
            $inDegree[$neighbor]--
            if ($inDegree[$neighbor] -eq 0) { $queue.Enqueue($neighbor) }
        }
    }

    # Cycle detection
    if ($sorted.Count -ne $Targets.Count) {
        $cycleTargets = $Targets | Where-Object { (Get-CdoProperty -Object $_ -Name 'name') -notin $sorted }
        $names = ($cycleTargets | ForEach-Object { Get-CdoProperty -Object $_ -Name 'name' }) -join ', '
        throw "Circular dependency detected involving: $names"
    }

    return @($sorted | ForEach-Object { $targetMap[$_] })
}

function Invoke-CdoTargetCreate {
    param(
        [string]$Root,
        [string]$Name,
        [string]$Type,           # "executable" or "library"
        [string]$Linkage,        # "static" or "shared" (required for library)
        [string]$Skeleton        # optional skeleton template name
    )

    # Load manifest
    $manifestPath = Join-Path $Root 'project.cdo.json'
    $config = Read-CdoJson -Path $manifestPath
    if ($null -eq $config) {
        throw "No project.cdo.json found at '$Root'. Run 'cdo init' first."
    }

    # Convert to mutable hashtable for manipulation
    $config = Convert-CdoObjectToHashtable -InputObject $config

    # Get existing targets array
    $targets = @(Get-CdoProperty -Object $config -Name 'targets' -Default @())

    # Validate target name format
    if (-not (Test-CdoTargetName -Name $Name)) {
        throw "Target name must start with a letter and contain only lowercase letters, digits, underscores, or hyphens."
    }

    # Validate uniqueness
    if (Test-CdoTargetExists -Targets $targets -Name $Name) {
        throw "Target '$Name' already exists. Use 'cdo target list' to see declared targets."
    }

    if ($Type -eq 'executable') {
        # Create target entry
        $targetEntry = [ordered]@{
            name = $Name
            type = 'executable'
            target = $Name
            workingDirectory = '.'
            args = @()
            dependencies = @()
        }

        # Create test target entry
        $testEntry = [ordered]@{
            name = "${Name}_tests"
            type = 'test'
            target = "${Name}_tests"
            testFor = $Name
        }

        # Add both to targets array
        $targets += $targetEntry
        $targets += $testEntry
        Set-CdoProperty -Object $config -Name 'targets' -Value $targets

        # Create src/<name>/ directory with skeleton source file
        $srcDir = Join-Path $Root (Join-Path 'src' $Name)
        New-CdoDirectory -Path $srcDir

        $mainContent = Get-CdoTargetSkeletonSource -Name $Name -Type 'executable' -Skeleton $Skeleton
        $srcFileName = Get-CdoTargetSkeletonSourceFileName -Name $Name -Type 'executable' -Skeleton $Skeleton
        $mainPath = Join-Path $srcDir $srcFileName
        Write-CdoFileIfMissing -Path $mainPath -Content $mainContent

        # Create tests/<name>/ directory with skeleton test file
        $testDir = Join-Path $Root (Join-Path 'tests' $Name)
        New-CdoDirectory -Path $testDir

        $testContent = Get-CdoTargetSkeletonTest -Name $Name -Type 'executable' -Skeleton $Skeleton
        $testFileName = Get-CdoTargetSkeletonTestFileName -Name $Name -Type 'executable' -Skeleton $Skeleton
        $testPath = Join-Path $testDir $testFileName
        Write-CdoFileIfMissing -Path $testPath -Content $testContent

        # Save manifest
        Write-CdoJson -Path $manifestPath -Value $config

        # Regenerate CMakeLists.txt
        Write-CdoCMakeLists -Root $Root -Config $config

        Write-CdoLog -Level OK -Message "created executable target '$Name'"
        Write-CdoLog -Level OK -Message "created test target '${Name}_tests'"
    } elseif ($Type -eq 'library') {
        # Validate linkage is specified
        if ($Linkage -ne 'static' -and $Linkage -ne 'shared') {
            throw "Library targets require explicit linkage. Use --static or --shared."
        }

        # Create library target entry
        $targetEntry = [ordered]@{
            name = $Name
            type = 'library'
            linkage = $Linkage
            target = $Name
            dependencies = @()
        }

        # Create test target entry
        $testEntry = [ordered]@{
            name = "${Name}_tests"
            type = 'test'
            target = "${Name}_tests"
            testFor = $Name
        }

        # Add both to targets array
        $targets += $targetEntry
        $targets += $testEntry
        Set-CdoProperty -Object $config -Name 'targets' -Value $targets

        # Create src/<name>/ directory with skeleton source file
        $srcDir = Join-Path $Root (Join-Path 'src' $Name)
        New-CdoDirectory -Path $srcDir

        $srcContent = Get-CdoTargetSkeletonSource -Name $Name -Type 'library' -Skeleton $Skeleton
        $srcFileName = Get-CdoTargetSkeletonSourceFileName -Name $Name -Type 'library' -Skeleton $Skeleton
        $srcPath = Join-Path $srcDir $srcFileName
        Write-CdoFileIfMissing -Path $srcPath -Content $srcContent

        # Create include/<name>/ directory with skeleton header file
        $includeDir = Join-Path $Root (Join-Path 'include' $Name)
        New-CdoDirectory -Path $includeDir

        $headerContent = Get-CdoTargetSkeletonHeader -Name $Name -Type 'library'
        $headerFileName = Get-CdoTargetSkeletonHeaderFileName -Name $Name
        $headerPath = Join-Path $includeDir $headerFileName
        Write-CdoFileIfMissing -Path $headerPath -Content $headerContent

        # Create tests/<name>/ directory with skeleton test file
        $testDir = Join-Path $Root (Join-Path 'tests' $Name)
        New-CdoDirectory -Path $testDir

        $testContent = Get-CdoTargetSkeletonTest -Name $Name -Type 'library' -Skeleton $Skeleton
        $testFileName = Get-CdoTargetSkeletonTestFileName -Name $Name -Type 'library' -Skeleton $Skeleton
        $testPath = Join-Path $testDir $testFileName
        Write-CdoFileIfMissing -Path $testPath -Content $testContent

        # Save manifest
        Write-CdoJson -Path $manifestPath -Value $config

        # Regenerate CMakeLists.txt
        Write-CdoCMakeLists -Root $Root -Config $config

        Write-CdoLog -Level OK -Message "created library target '$Name' ($Linkage)"
        Write-CdoLog -Level OK -Message "created test target '${Name}_tests'"
    } else {
        throw "Unknown target type '$Type'. Use 'executable' or 'library'."
    }
}

function Invoke-CdoTargetDelete {
    param(
        [string]$Root,
        [string]$Name
    )

    # Load manifest
    $manifestPath = Join-Path $Root 'project.cdo.json'
    $config = Read-CdoJson -Path $manifestPath
    if ($null -eq $config) {
        throw "No project.cdo.json found at '$Root'. Run 'cdo init' first."
    }

    # Convert to mutable hashtable for manipulation
    $config = Convert-CdoObjectToHashtable -InputObject $config

    # Get existing targets array
    $targets = @(Get-CdoProperty -Object $config -Name 'targets' -Default @())

    # Validate target exists (only check non-test targets)
    $nonTestTargets = @($targets | Where-Object {
        $type = Get-CdoProperty -Object $_ -Name 'type'
        $type -ne 'test'
    })
    $targetEntry = Get-CdoTargetByName -Targets $nonTestTargets -Name $Name
    if ($null -eq $targetEntry) {
        $availableNames = @($nonTestTargets | ForEach-Object { Get-CdoProperty -Object $_ -Name 'name' }) -join ', '
        throw "Target '$Name' not found. Available targets: $availableNames"
    }

    # Check no other targets depend on this target
    $dependents = @($nonTestTargets | Where-Object {
        $tName = Get-CdoProperty -Object $_ -Name 'name'
        if ($tName -eq $Name) { return $false }
        $deps = @(Get-CdoProperty -Object $_ -Name 'dependencies' -Default @() | Where-Object { $_ })
        $deps -contains $Name
    })
    if ($dependents.Count -gt 0) {
        $dependentNames = @($dependents | ForEach-Object { Get-CdoProperty -Object $_ -Name 'name' }) -join ', '
        throw "Cannot delete '$Name': it is depended on by $dependentNames. Remove those dependencies first."
    }

    # Remove the target entry from targets array
    $testTargetName = "${Name}_tests"
    $updatedTargets = @($targets | Where-Object {
        $tName = Get-CdoProperty -Object $_ -Name 'name'
        $testFor = Get-CdoProperty -Object $_ -Name 'testFor' -Default ''
        # Keep if not the target AND not its test target
        ($tName -ne $Name) -and ($tName -ne $testTargetName) -and ($testFor -ne $Name)
    })

    # Update config with filtered targets
    Set-CdoProperty -Object $config -Name 'targets' -Value $updatedTargets

    # Save manifest
    Write-CdoJson -Path $manifestPath -Value $config

    # Check if any non-test targets remain
    $remainingNonTest = @($updatedTargets | Where-Object {
        $type = Get-CdoProperty -Object $_ -Name 'type'
        $type -ne 'test'
    })

    $cmakePath = Join-Path $Root 'CMakeLists.txt'
    if ($remainingNonTest.Count -eq 0) {
        # Remove CMakeLists.txt if no targets remain
        if (Test-Path -LiteralPath $cmakePath) {
            Remove-Item -LiteralPath $cmakePath -Force
            Write-CdoLog -Level OK -Message "removed CMakeLists.txt (no targets remain)"
        }
    } else {
        # Regenerate CMakeLists.txt
        Write-CdoCMakeLists -Root $Root -Config $config
    }

    # Print message that source files were retained on disk
    Write-CdoLog -Level OK -Message "Deleted target '$Name'. Source files retained at src/$Name/, tests/$Name/"
}

function Invoke-CdoTargetList {
    param([string]$Root)

    # Load manifest
    $manifestPath = Join-Path $Root 'project.cdo.json'
    $config = Read-CdoJson -Path $manifestPath
    if ($null -eq $config) {
        throw "No project.cdo.json found at '$Root'. Run 'cdo init' first."
    }

    # Get targets array
    $allTargets = @(Get-CdoProperty -Object $config -Name 'targets' -Default @())

    # Filter to non-test targets only
    $nonTestTargets = @($allTargets | Where-Object {
        $type = Get-CdoProperty -Object $_ -Name 'type'
        $type -ne 'test'
    })

    # If empty, display message and return
    if ($nonTestTargets.Count -eq 0) {
        Write-CdoLog -Level INFO -Message "No targets declared."
        return
    }

    # Build display data for each non-test target
    $displayRows = @()
    foreach ($target in $nonTestTargets) {
        $name = Get-CdoProperty -Object $target -Name 'name'
        $type = Get-CdoProperty -Object $target -Name 'type'
        $linkage = Get-CdoProperty -Object $target -Name 'linkage' -Default ''
        $deps = @(Get-CdoProperty -Object $target -Name 'dependencies' -Default @() | Where-Object { $_ })

        # Build type column: "executable", "library (static)", or "library (shared)"
        $typeDisplay = $type
        if ($type -eq 'library' -and $linkage) {
            $typeDisplay = "library ($linkage)"
        }

        # Build dependencies column
        $depsDisplay = ''
        if ($deps.Count -gt 0) {
            $depsDisplay = "depends on: " + ($deps -join ', ')
        }

        $displayRows += [pscustomobject]@{
            Name = $name
            Type = $typeDisplay
            Deps = $depsDisplay
        }
    }

    # Calculate column widths for alignment
    $nameWidth = ($displayRows | ForEach-Object { $_.Name.Length } | Measure-Object -Maximum).Maximum
    $typeWidth = ($displayRows | ForEach-Object { $_.Type.Length } | Measure-Object -Maximum).Maximum

    # Ensure minimum widths for readability
    if ($nameWidth -lt 4) { $nameWidth = 4 }
    if ($typeWidth -lt 4) { $typeWidth = 4 }

    # Print header
    Write-Host "Targets:"

    # Print each target row with aligned columns
    foreach ($row in $displayRows) {
        $line = "  " + $row.Name.PadRight($nameWidth + 2) + $row.Type.PadRight($typeWidth + 4)
        if ($row.Deps) {
            $line += $row.Deps
        }
        Write-Host $line.TrimEnd()
    }
}

function Get-CdoTargetSourceFiles {
    param(
        [string]$Root,
        [string]$TargetName,
        [string]$SourceDir = 'src'
    )

    $targetSrcDir = Join-Path $Root (Join-Path $SourceDir $TargetName)
    if (-not (Test-Path -LiteralPath $targetSrcDir)) {
        return @()
    }

    $extensions = @('.c', '.cpp', '.cc', '.cxx')
    $files = @(Get-ChildItem -LiteralPath $targetSrcDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $extensions -contains $_.Extension.ToLowerInvariant() })

    $relativePaths = @()
    foreach ($file in $files) {
        $relative = Get-CdoRelativePath -From $Root -To $file.FullName
        $relative = ConvertTo-CdoForwardSlashPath -Path $relative
        while ($relative.StartsWith('./')) { $relative = $relative.Substring(2) }
        $relativePaths += $relative
    }

    return @($relativePaths | Sort-Object)
}

function Write-CdoCMakeLists {
    param(
        [string]$Root,
        [object]$Config
    )

    # Read project metadata
    $projectName = Get-CdoProperty -Object $Config -Name 'name' -Default 'project'
    $projectVersion = Get-CdoProperty -Object $Config -Name 'version' -Default '0.1.0'
    $cStandard = Get-CdoProperty -Object $Config -Name 'cStandard' -Default 17
    $cppStandard = Get-CdoProperty -Object $Config -Name 'cppStandard' -Default 20

    # Get targets and separate non-test targets
    $allTargets = @(Get-CdoProperty -Object $Config -Name 'targets' -Default @())
    $nonTestTargets = @($allTargets | Where-Object {
        $type = Get-CdoProperty -Object $_ -Name 'type'
        $type -ne 'test'
    })

    # Sort non-test targets topologically
    $sortedTargets = @()
    if ($nonTestTargets.Count -gt 0) {
        $sortedTargets = @(Get-CdoTargetBuildOrder -Targets $nonTestTargets)
    }

    # Build CMake content
    $lines = [System.Collections.ArrayList]::new()

    # cmake_minimum_required and project()
    [void]$lines.Add("cmake_minimum_required(VERSION 3.20)")
    [void]$lines.Add("project($projectName VERSION $projectVersion LANGUAGES C CXX)")
    [void]$lines.Add("")

    # C/C++ standard settings
    [void]$lines.Add("set(CMAKE_C_STANDARD $cStandard)")
    [void]$lines.Add("set(CMAKE_C_STANDARD_REQUIRED ON)")
    [void]$lines.Add("set(CMAKE_C_EXTENSIONS OFF)")
    [void]$lines.Add("")
    [void]$lines.Add("set(CMAKE_CXX_STANDARD $cppStandard)")
    [void]$lines.Add("set(CMAKE_CXX_STANDARD_REQUIRED ON)")
    [void]$lines.Add("set(CMAKE_CXX_EXTENSIONS OFF)")
    [void]$lines.Add("")

    # Option for tests
    [void]$lines.Add('option(CDO_ENABLE_TESTS "Build cdo-generated tests" ON)')
    [void]$lines.Add("")

    # External dependency integration
    [void]$lines.Add("include(cmake/cdo_deps.cmake OPTIONAL)")
    [void]$lines.Add("")

    # Function stub guards
    [void]$lines.Add("if(NOT COMMAND cdo_apply_dependencies)")
    [void]$lines.Add("    function(cdo_apply_dependencies target)")
    [void]$lines.Add("    endfunction()")
    [void]$lines.Add("endif()")
    [void]$lines.Add("")
    [void]$lines.Add("if(NOT COMMAND cdo_copy_runtime_dependencies)")
    [void]$lines.Add("    function(cdo_copy_runtime_dependencies target)")
    [void]$lines.Add("    endfunction()")
    [void]$lines.Add("endif()")
    [void]$lines.Add("")

    # Emit each non-test target in topological order
    foreach ($target in $sortedTargets) {
        $name = Get-CdoProperty -Object $target -Name 'name'
        $type = Get-CdoProperty -Object $target -Name 'type'
        $linkage = Get-CdoProperty -Object $target -Name 'linkage' -Default ''
        $cmakeTarget = Get-CdoProperty -Object $target -Name 'target' -Default $name
        $deps = @(Get-CdoProperty -Object $target -Name 'dependencies' -Default @() | Where-Object { $_ })

        # Discover source files for this target
        $sourceFiles = @(Get-CdoTargetSourceFiles -Root $Root -TargetName $name)

        # Determine type label for the comment
        $typeLabel = $type
        if ($type -eq 'library') {
            $typeLabel = "$linkage library"
        }

        [void]$lines.Add("# --- Target: $name ($typeLabel) ---")

        if ($type -eq 'executable') {
            if ($sourceFiles.Count -gt 0) {
                [void]$lines.Add("add_executable($cmakeTarget")
                foreach ($src in $sourceFiles) {
                    [void]$lines.Add("    $src")
                }
                [void]$lines.Add(")")
            } else {
                [void]$lines.Add("# No source files found yet in src/$name/")
                [void]$lines.Add("add_executable($cmakeTarget")
                [void]$lines.Add("    src/$name/main.cpp")
                [void]$lines.Add(")")
            }
        } elseif ($type -eq 'library') {
            $linkageKeyword = if ($linkage -eq 'shared') { 'SHARED' } else { 'STATIC' }
            if ($sourceFiles.Count -gt 0) {
                [void]$lines.Add("add_library($cmakeTarget $linkageKeyword")
                foreach ($src in $sourceFiles) {
                    [void]$lines.Add("    $src")
                }
                [void]$lines.Add(")")
            } else {
                [void]$lines.Add("# No source files found yet in src/$name/")
                [void]$lines.Add("add_library($cmakeTarget $linkageKeyword")
                [void]$lines.Add("    src/$name/$name.cpp")
                [void]$lines.Add(")")
            }

            # target_include_directories for library targets
            [void]$lines.Add("target_include_directories($cmakeTarget")
            [void]$lines.Add("    PUBLIC")
            [void]$lines.Add("        include/$name")
            [void]$lines.Add(")")
        }

        # target_link_libraries for internal dependencies
        if ($deps.Count -gt 0) {
            [void]$lines.Add("target_link_libraries($cmakeTarget")
            [void]$lines.Add("    PRIVATE")
            foreach ($dep in $deps) {
                # Resolve the dependency's cmake target name
                $depTarget = Get-CdoTargetByName -Targets $allTargets -Name $dep
                $depCmakeTarget = $dep
                if ($null -ne $depTarget) {
                    $depCmakeTarget = Get-CdoProperty -Object $depTarget -Name 'target' -Default $dep
                }
                [void]$lines.Add("        $depCmakeTarget")
            }
            [void]$lines.Add(")")
        }

        # cdo_apply_dependencies for every target
        [void]$lines.Add("cdo_apply_dependencies($cmakeTarget)")

        # cdo_copy_runtime_dependencies for executables
        if ($type -eq 'executable') {
            [void]$lines.Add("cdo_copy_runtime_dependencies($cmakeTarget)")

            # DLL post-build copy commands for shared library dependencies
            $sharedLibDeps = @(Get-CdoSharedLibDependencies -Target $target -AllTargets $allTargets)
            if ($sharedLibDeps.Count -gt 0) {
                [void]$lines.Add("")
                [void]$lines.Add("# Copy shared library DLLs to executable output directory")
                foreach ($sharedLib in $sharedLibDeps) {
                    $sharedLibCmakeTarget = Get-CdoProperty -Object $sharedLib -Name 'target' -Default (Get-CdoProperty -Object $sharedLib -Name 'name')
                    [void]$lines.Add("add_custom_command(TARGET $cmakeTarget POST_BUILD")
                    [void]$lines.Add("    COMMAND `"`${CMAKE_COMMAND}`" -E copy_if_different")
                    [void]$lines.Add("        `"`$<TARGET_FILE:$sharedLibCmakeTarget>`"")
                    [void]$lines.Add("        `"`$<TARGET_FILE_DIR:$cmakeTarget>`"")
                    [void]$lines.Add("    VERBATIM")
                    [void]$lines.Add(")")
                }
            }
        }

        [void]$lines.Add("")
    }

    # --- Test targets section ---
    $testTargets = @($allTargets | Where-Object {
        $type = Get-CdoProperty -Object $_ -Name 'type'
        $type -eq 'test'
    })

    if ($testTargets.Count -gt 0) {
        [void]$lines.Add("if(CDO_ENABLE_TESTS)")
        [void]$lines.Add("    enable_testing()")
        [void]$lines.Add("")

        foreach ($testTarget in $testTargets) {
            $testName = Get-CdoProperty -Object $testTarget -Name 'name'
            $testCmakeTarget = Get-CdoProperty -Object $testTarget -Name 'target' -Default $testName
            $testFor = Get-CdoProperty -Object $testTarget -Name 'testFor' -Default ''

            # Determine the tested target
            $testedTarget = $null
            if ($testFor) {
                $testedTarget = Get-CdoTargetByName -Targets $allTargets -Name $testFor
            }

            # Discover test source files from tests/<tested_target_name>/
            $testSourceDir = 'tests'
            $testSourceName = if ($testFor) { $testFor } else { $testName }
            $testSourceFiles = @(Get-CdoTargetSourceFiles -Root $Root -TargetName $testSourceName -SourceDir $testSourceDir)

            [void]$lines.Add("    # --- Test: $testCmakeTarget ---")

            # add_executable for test target
            if ($testSourceFiles.Count -gt 0) {
                [void]$lines.Add("    add_executable($testCmakeTarget")
                foreach ($src in $testSourceFiles) {
                    [void]$lines.Add("        $src")
                }
                [void]$lines.Add("    )")
            } else {
                [void]$lines.Add("    # No test source files found yet in tests/$testSourceName/")
                [void]$lines.Add("    add_executable($testCmakeTarget")
                [void]$lines.Add("        tests/$testSourceName/test_$testSourceName.cpp")
                [void]$lines.Add("    )")
            }

            # target_link_libraries against the tested target and its dependencies
            if ($null -ne $testedTarget) {
                $testedCmakeTarget = Get-CdoProperty -Object $testedTarget -Name 'target' -Default $testFor
                $testedType = Get-CdoProperty -Object $testedTarget -Name 'type'
                $testedDeps = @(Get-CdoProperty -Object $testedTarget -Name 'dependencies' -Default @() | Where-Object { $_ })

                # For library targets, link against the library itself
                # For executable targets, link against the executable's dependencies (can't link against an exe)
                $linkTargets = @()
                if ($testedType -ne 'executable') {
                    $linkTargets += $testedCmakeTarget
                }

                # Also link against the tested target's dependencies for transitive linkage
                foreach ($dep in $testedDeps) {
                    $depTarget = Get-CdoTargetByName -Targets $allTargets -Name $dep
                    $depCmakeTarget = $dep
                    if ($null -ne $depTarget) {
                        $depCmakeTarget = Get-CdoProperty -Object $depTarget -Name 'target' -Default $dep
                    }
                    $linkTargets += $depCmakeTarget
                }

                if ($linkTargets.Count -gt 0) {
                    [void]$lines.Add("    target_link_libraries($testCmakeTarget")
                    [void]$lines.Add("        PRIVATE")
                    foreach ($lt in $linkTargets) {
                        [void]$lines.Add("            $lt")
                    }
                    [void]$lines.Add("    )")
                }
            }

            # cdo_apply_dependencies for test target
            [void]$lines.Add("    cdo_apply_dependencies($testCmakeTarget)")

            # DLL copy commands for test targets that depend on shared libraries
            if ($null -ne $testedTarget) {
                $sharedLibDeps = @(Get-CdoSharedLibDependencies -Target $testedTarget -AllTargets $nonTestTargets)

                # Also check if the tested target itself is a shared library
                $testedType = Get-CdoProperty -Object $testedTarget -Name 'type'
                $testedLinkage = Get-CdoProperty -Object $testedTarget -Name 'linkage' -Default ''
                if ($testedType -eq 'library' -and $testedLinkage -eq 'shared') {
                    # Include the tested target itself as a shared lib that needs copying
                    $sharedLibDeps = @($testedTarget) + @($sharedLibDeps)
                }

                if ($sharedLibDeps.Count -gt 0) {
                    [void]$lines.Add("    cdo_copy_runtime_dependencies($testCmakeTarget)")

                    foreach ($sharedLib in $sharedLibDeps) {
                        $sharedLibCmakeTarget = Get-CdoProperty -Object $sharedLib -Name 'target' -Default (Get-CdoProperty -Object $sharedLib -Name 'name')
                        [void]$lines.Add("    add_custom_command(TARGET $testCmakeTarget POST_BUILD")
                        [void]$lines.Add('        COMMAND "${CMAKE_COMMAND}" -E copy_if_different')
                        [void]$lines.Add("            `"`$<TARGET_FILE:$sharedLibCmakeTarget>`"")
                        [void]$lines.Add("            `"`$<TARGET_FILE_DIR:$testCmakeTarget>`"")
                        [void]$lines.Add("        VERBATIM")
                        [void]$lines.Add("    )")
                    }
                }
            }

            # add_test
            [void]$lines.Add("    add_test(NAME $testCmakeTarget COMMAND $testCmakeTarget)")
            [void]$lines.Add("")
        }

        [void]$lines.Add("endif()")
        [void]$lines.Add("")
    }

    # Write the content to CMakeLists.txt
    $content = ($lines.ToArray() -join "`n") + "`n"
    $cmakePath = Join-Path $Root 'CMakeLists.txt'
    Write-CdoTextFile -Path $cmakePath -Content $content
    Write-CdoLog -Level OK -Message "generated CMakeLists.txt"
}
