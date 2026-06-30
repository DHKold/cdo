# CDo Development Rules

## Code Style

- Lines can be up to 240 characters. Don't wrap at 80. Use newlines and spaces for readability, not arbitrary line limits.
- Keep code concise but readable: meaningful comments, well-named functions, clear intent.
- Organize files in small units: aim for <500 lines per file. Prefer small reusable functions composed together.
- If a file is growing, consider splitting it. If they are more than 2 files for a coherent set, create a subfolder with a proper structure. (The api SHALL follow the split and restructuration)
- Use folders to organize related files, not name prefixes. Example: `commands/build/compile.c` rather than `commands/build_compile.c`.
- This applies to ALL directories including `api/` (public headers). Headers follow the same folder structure as their source: `api/cmd/cli_cmd.h`, `api/out/cli_out.h`, `api/term/cli_term.h`. Never use flat `api/cli_*.h` prefixes.
- Add extensive logging with the correct levels

## Development Methodology

- Design the code from top to bottom with genericity, reusability, testability and security in mind
    - Start with the top requirement (what the feature must DO) to derive the required functions
    - Split the big functions into smaller generic steps and focus the top functions on orchestration of generic calls
    - Group functions by domain (File System Utils > File Reading, Structured Data > Json Parser, etc.), not by feature / command
    - Ensure the function handles all cases (including errors, edge cases, etc.) properly
- Never use stubs, TODO or temporary code
- Prefer TDD (Test Driven Development):
    1. Write the interfaces
    2. Write the unit tests (Don't compile/run them yet, don't stub/mock the implementation)
    3. Write the implementation
    4. Run the unit tests
- No PBT (Property Based Testing). Use extensive unit testing targeting >90% line coverage on all touched source files.
- Maintain the README.md up to date and aligned with the state of the code.
- If you see that two functions can be merged as a generic one, evaluate if it would make the code clearer and more readable
- In case an unforeseen design issue is discovered, ask the user for precisions

## Building & Testing

- You SHALL use `./cdo.exe` to manage the workspace. Main commands:
    - `cdo build <crate>` : Builds the given crate (all crates if omitted). Useful arguments: '--release', '--verbose', '--jobs 5'
    - `cdo test <crate>`  : Build and test the given crate (all crates if omitted). Useful arguments: build ones, '--coverage', '--filter <prefix>'
    - `cdo e2e <crate>`   : Build and run the E2E on the given crate (all crates if omitted). Useful arguments: '--filter <prefix>'
    - `cdocdo fmt`<crate> : Format source files on the given crate (all crates if omitted). Useful arguments: '--check'
- You CANNOT replace `./cdo.exe`.
- To test a built artifact (like a built cdo.exe), copy it from the build location to a temporary root file (like `cdo_latest.exe`)
- The E2E tests of cdo (crates/cdo/e2e) SHALL be maintained and extended to cover all commands and arguments of the cdo binary.
    - Only add the E2E tests relevant to the current feature
    - Use well though fixtures that can be reused for multiple E2E tests.
    - Create small dedicated E2E tests rather than big ones
- If a bug appears and no Test (Unit or E2E) is detecting it, implement a regression test for it
    - Unit Test is preferred when possible
- Note about locking: when a build is in progress, cdo creates a lock (.cd/build.lock) to prevent conflicts. Do NOT remove the lock manually unless the process that created the lock no longer exist, but this should not happen normaly as the lock is removed in any case when the build stops (success or failure).

## Versioning

- When starting on a feature, create a new branch
- Commit coherant set of tasks that can be easily reviewed and tested
- When the feature is done, do a final code review to assess Code Quality
- The use is responsible for merging the branch

## Feature Validation

A feature CANNOT be marked as done unless:

- The build fully succeed for the entire workspace in release mode (`./cdo.exe build --release`)
- The unit tests succeed for the entire workspace with coverage >90% on touched source (`./cdo.exe test --coverage`)
- The e2e tests succeed for the entire workspace (`./cdo.exe e2e`)
- The sources are successfully formated (`./cdo.exe fmt`)

And also, using the release built of cdo (copy `build/release/cdo/cdo.exe` to `cdo_latest.exe`), all the above must also work perfectly.

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
