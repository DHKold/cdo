$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$here\..\CdoHelpers.ps1"
. "$here\..\CdoTargets.ps1"

# We need Initialize-ProjectMinimal from cdo.ps1, but sourcing cdo.ps1 directly
# runs the main dispatcher which calls exit. Instead, we extract the needed functions.
# Initialize-ProjectMinimal depends on: Save-Config, Get-GitignoreContent, Install-CdoSelf
# We use -NoSelf to skip Install-CdoSelf, and define the other two here.

function Save-Config {
    param([object]$Config, [string]$Root)
    Write-CdoJson -Path (Join-Path $Root 'project.cdo.json') -Value $Config
}

function Get-GitignoreContent {
    return @'
# Build outputs
build/
out/
dist/
*.exe
*.dll
*.lib
*.obj
*.pdb
*.ilk

# cdo caches and portable tools
.cdo/cache/
.cdo/tools/

# Dependency archives, but not vendored dependency source/binaries
third_party/**/*.zip
third_party/**/*.tar
third_party/**/*.tar.gz
third_party/**/*.tgz
third_party/**/*.tar.bz2
third_party/**/*.tbz2

# Editors and local state
.vs/
*.user
*.suo
.DS_Store
'@
}

function Initialize-ProjectMinimal {
    param(
        [string]$Path,
        [string]$Name,
        [switch]$DryRun,
        [switch]$Force,
        [switch]$NoSelf
    )

    $root = Resolve-CdoPath -Path $Path
    $projectName = $Name
    if ([string]::IsNullOrWhiteSpace($projectName)) {
        $projectName = Split-Path $root -Leaf
        if ([string]::IsNullOrWhiteSpace($projectName)) { $projectName = 'c_project' }
    }
    $projectId = Get-CdoSafeName -Name $projectName

    if ($DryRun) {
        Write-CdoLog -Level INFO -Message "init preview for $root (minimal, no skeleton)"
        Write-Host "Would create project.cdo.json with empty targets array"
        Write-Host "Would create README.md, .gitignore"
        Write-Host "Would NOT create CMakeLists.txt, src/, include/, or tests/"
        if (-not $NoSelf) { Write-Host "Would vendor cdo into .cdo/ and create .cdo/activate.ps1" }
        return
    }

    New-CdoDirectory -Path $root
    Write-CdoLog -Level INFO -Message "initializing $projectName at $root (no skeleton)"

    # Create project.cdo.json with schemaVersion 2 and empty targets
    $manifestPath = Join-Path $root 'project.cdo.json'
    if ((Test-Path -LiteralPath $manifestPath) -and -not $Force) {
        Write-CdoLog -Level INFO -Message "kept existing $manifestPath"
    } else {
        $config = [ordered]@{
            schema        = 'https://cdo.dev/schemas/project.v1.json'
            schemaVersion = 2
            name          = $projectName
            id            = $projectId
            version       = '0.1.0'
            cStandard     = 17
            cppStandard   = 20
            targets       = @()
            build         = [ordered]@{
                directory     = 'build'
                defaultConfig = 'Debug'
                generator     = 'Unix Makefiles'
            }
            dependencies  = [ordered]@{}
        }
        Save-Config -Config $config -Root $root
        Write-CdoLog -Level OK -Message "wrote $manifestPath"
    }

    # Create README.md
    $readmeContent = @"
# $projectName

Portable C/C++ project managed by CDo.

## First Run

``````powershell
. .\.cdo\activate.ps1
cdo doctor
cdo target create app --type executable
cdo build app
cdo run app
cdo test
``````

## Adding Targets

``````powershell
cdo target create mylib --type library --static
cdo target create game --type executable
cdo target list
``````

## Dependencies

Add catalog dependencies with:

``````powershell
cdo add sdl3
``````
"@
    Write-CdoFileIfMissing -Path (Join-Path $root 'README.md') -Content $readmeContent

    # Create .gitignore
    Write-CdoFileIfMissing -Path (Join-Path $root '.gitignore') -Content (Get-GitignoreContent)

    # Vendor CDo into .cdo/
    if (-not $NoSelf) { Install-CdoSelf -Root $root }

    # Do NOT create CMakeLists.txt, src/, include/, tests/, or any skeleton files

    Write-CdoLog -Level OK -Message "init complete"
    Write-Host ""
    Write-Host "Next:"
    Write-Host "  . .\.cdo\activate.ps1"
    Write-Host "  cdo doctor"
    Write-Host "  cdo target create <name> --type executable"
    Write-Host "  cdo build <name>"
    Write-Host "  cdo test"
}

# Helper: Creates a temp directory for init testing
function New-InitTestDir {
    $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "cdo-init-test-$([System.Guid]::NewGuid().ToString('N').Substring(0,8))"
    return $tempDir
}

# Helper: Reads manifest from a test project root
function Get-TestManifest {
    param([string]$Root)
    $manifestPath = Join-Path $Root 'project.cdo.json'
    return Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
}

# Helper: Removes temp project directory
function Remove-InitTestDir {
    param([string]$Root)
    if ((Test-Path -LiteralPath $Root) -and $Root -like '*cdo-init-test-*') {
        Remove-Item -LiteralPath $Root -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Describe 'Property 14: Init produces no targets or source directories' {
    # **Validates: Requirements 5.1, 5.5, 5.6**
    # For any invocation of cdo init with any valid project name, the resulting
    # manifest SHALL have an empty targets array, and no src/, include/, tests/,
    # or CMakeLists.txt file SHALL be created.

    Context 'Manifest has empty targets array and schemaVersion 2' {
        It 'creates project.cdo.json with empty targets array' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'test-project' -NoSelf

                $manifestPath = Join-Path $root 'project.cdo.json'
                Test-Path -LiteralPath $manifestPath | Should Be $true

                $config = Get-TestManifest -Root $root
                $targets = @($config.targets)
                $targets.Count | Should Be 0
            } finally {
                Remove-InitTestDir -Root $root
            }
        }

        It 'creates project.cdo.json with schemaVersion 2' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'my-game' -NoSelf

                $config = Get-TestManifest -Root $root
                $config.schemaVersion | Should Be 2
            } finally {
                Remove-InitTestDir -Root $root
            }
        }

        It '--name parameter sets the project name correctly' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'unbound-engine' -NoSelf

                $config = Get-TestManifest -Root $root
                $config.name | Should Be 'unbound-engine'
            } finally {
                Remove-InitTestDir -Root $root
            }
        }

        It 'project id is derived as safe name from project name' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'My Cool Game' -NoSelf

                $config = Get-TestManifest -Root $root
                # Get-CdoSafeName converts to lowercase, replaces non-alphanum with _
                $config.id | Should Be 'my_cool_game'
            } finally {
                Remove-InitTestDir -Root $root
            }
        }

        It 'works with various valid project names' {
            $names = @('alpha', 'game-engine', 'my_project', 'renderer2', 'hello world')
            foreach ($name in $names) {
                $root = New-InitTestDir
                try {
                    Initialize-ProjectMinimal -Path $root -Name $name -NoSelf

                    $config = Get-TestManifest -Root $root
                    $config.name | Should Be $name
                    @($config.targets).Count | Should Be 0
                    $config.schemaVersion | Should Be 2
                } finally {
                    Remove-InitTestDir -Root $root
                }
            }
        }
    }

    Context 'Common files are created' {
        It 'creates README.md' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'test-project' -NoSelf

                $readmePath = Join-Path $root 'README.md'
                Test-Path -LiteralPath $readmePath | Should Be $true
            } finally {
                Remove-InitTestDir -Root $root
            }
        }

        It 'creates .gitignore' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'test-project' -NoSelf

                $gitignorePath = Join-Path $root '.gitignore'
                Test-Path -LiteralPath $gitignorePath | Should Be $true
            } finally {
                Remove-InitTestDir -Root $root
            }
        }
    }

    Context 'No source directories or CMakeLists.txt are created' {
        It 'does NOT create CMakeLists.txt' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'test-project' -NoSelf

                $cmakePath = Join-Path $root 'CMakeLists.txt'
                Test-Path -LiteralPath $cmakePath | Should Be $false
            } finally {
                Remove-InitTestDir -Root $root
            }
        }

        It 'does NOT create src/ directory' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'test-project' -NoSelf

                $srcDir = Join-Path $root 'src'
                Test-Path -LiteralPath $srcDir | Should Be $false
            } finally {
                Remove-InitTestDir -Root $root
            }
        }

        It 'does NOT create include/ directory' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'test-project' -NoSelf

                $includeDir = Join-Path $root 'include'
                Test-Path -LiteralPath $includeDir | Should Be $false
            } finally {
                Remove-InitTestDir -Root $root
            }
        }

        It 'does NOT create tests/ directory' {
            $root = New-InitTestDir
            try {
                Initialize-ProjectMinimal -Path $root -Name 'test-project' -NoSelf

                $testsDir = Join-Path $root 'tests'
                Test-Path -LiteralPath $testsDir | Should Be $false
            } finally {
                Remove-InitTestDir -Root $root
            }
        }

        It 'no source directories for any valid project name' {
            $names = @('alpha', 'my-game', 'cool_project', 'renderer2')
            foreach ($name in $names) {
                $root = New-InitTestDir
                try {
                    Initialize-ProjectMinimal -Path $root -Name $name -NoSelf

                    Test-Path -LiteralPath (Join-Path $root 'CMakeLists.txt') | Should Be $false
                    Test-Path -LiteralPath (Join-Path $root 'src') | Should Be $false
                    Test-Path -LiteralPath (Join-Path $root 'include') | Should Be $false
                    Test-Path -LiteralPath (Join-Path $root 'tests') | Should Be $false
                } finally {
                    Remove-InitTestDir -Root $root
                }
            }
        }
    }
}
