# CDo Build Instructions

## Primary Build Method: `cdo.exe`

Always use the CDo build tool itself to compile the project. Use the cdo.exe at the project's root (not the one in the build folder).

```
.\cdo.exe build
```

To build a specific crate:

```
.\cdo.exe build cdo
```

Note about locking: when a build is in progress, cdo creates a lock to prevent conflicts. Do NOT remove the lock manually unless the process that created the lock no longer exist, but this should not happen normaly as the lock is removed in any case when the build stops (success or failure).

Useful flags:

- `--verbose` or `-v` — show DEBUG-level output for troubleshooting
- `--quiet` or `-q` — suppress everything except errors
- `--release` or `-r` — build with optimizations

## Testing a built cdo

Don't run the `cdo.exe`from the build folder as it may try to compile and write itself, failing due to the file being used.
If you need to test a build, first copy the cdo.exe as `./cdo_temp.exe`

## Key Paths

| Artifact | Path |
|----------|------|
| CDo release binary | `build\release\cdo\cdo.exe` |
| CDo debug binary | `build\debug\cdo\cdo.exe` |
| Workspace root | (this repo root) |

# CDo Testing framework

To test a create (run the tests) use:

```
.\cdo.exe test <crate>
```

To get the coverage, use the `--coverage` argument

Check the test help for more information