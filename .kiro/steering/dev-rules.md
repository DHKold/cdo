# CDo Development Rules

## Code Style

- Lines can be up to 240 characters. Don't wrap at 80. Use newlines and spaces for readability, not arbitrary line limits.
- Keep code concise but readable: meaningful comments, well-named functions, clear intent.
- Organize files in small units: aim for <500 lines per file. Prefer small reusable functions composed together.
- Use folders to organize related files, not name prefixes. Example: `commands/build/compile.c` rather than `commands/build_compile.c`.

## Development Methodology

- Prefer TDD (Test Driven Development): write the interfaces first, then the unit tests, then the implementation.
- No PBT (Property Based Testing). Use extensive unit testing targeting >90% line coverage on all touched source files.
- When testing the produced `cdo.exe` (from the build folder), create workspaces in `e2e/<workspace_name>/`.
- Always prefer using `cdo` commands rather than manually editing workspace files (use `cdo init`, `cdo build`, etc.).

## Conventions

- Log any pain point, bug, or missing feature encountered during development so it can be tracked and handled later. Use `TODO:` or `FIXME:` comments in code, or note them in the spec's notes section.

## PAL Return Code Convention

All PAL functions (including `pal_path_exists`) follow this convention:
- Return `0` (PAL_OK) on **success** (e.g., path exists, file read successfully)
- Return non-zero error code on **failure** (e.g., `PAL_ERR_NOT_FOUND` = 9 when path does not exist)

This is the opposite of a boolean "exists" check. Think of it as "did the operation succeed?" not "does it exist?".
- `pal_path_exists(path) == 0` → path **exists**
- `pal_path_exists(path) != 0` → path **does NOT exist**

Never confuse this with POSIX `access()` (returns 0 on success) or Windows `PathFileExists` (returns TRUE/1 on exists). The PAL always uses error-code semantics: 0 = success, non-zero = failure.
