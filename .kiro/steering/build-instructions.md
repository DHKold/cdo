# CDo Build Instructions

## Primary Build Method: `cdo.exe`

Always use the CDo build tool itself to compile the project:

```
cdo.exe build
```

To build a specific crate:

```
cdo.exe build cdo
```

To build and run tests:

```
cdo.exe build cdo_pbt
.\build\release\cdo_pbt\cdo_pbt.exe
```

Useful flags:

- `--verbose` or `-v` — show DEBUG-level output for troubleshooting
- `--quiet` or `-q` — suppress everything except errors
- `--release` or `-r` — build with optimizations

## Fallback Only: `build.ps1`

The `build.ps1` script is a bootstrap/fallback build that compiles CDo directly with GCC. It does NOT use `cdo.exe` and should ONLY be used when:

- `cdo.exe` does not exist yet (first-time bootstrap)
- `cdo.exe` is broken and cannot build itself

If you must use it:

```powershell
powershell -File build.ps1         # Build cdo.exe
powershell -File build.ps1 -Test   # Build and run tests
powershell -File build.ps1 -Clean  # Remove build artifacts
```

## Key Paths

| Artifact | Path |
|----------|------|
| CDo release binary | `build\release\cdo\cdo.exe` |
| CDo debug binary | `build\debug\cdo\cdo.exe` |
| Test binary | `build\release\cdo_pbt\cdo_pbt.exe` |
| Workspace root | (this repo root) |
