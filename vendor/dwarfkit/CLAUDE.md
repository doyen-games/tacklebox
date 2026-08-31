# Dwarfkit

Read BLUEPRINT.md and PROGRESS.md first. Work the next unchecked item in PROGRESS.md.

Porting procedure, per file:
1. Read the TS source in reference/<repo>/src/ before writing anything. Never reconstruct Wharfkit from memory.
2. Write the header and implementation under the mirrored path, keeping names.
3. Port the matching test file and fixtures, run it, make it green.
4. Record any intentional deviation in DIVERGENCES.md with a one-line reason.
5. Tick PROGRESS.md, update PORT_MANIFEST.md, commit.

Style:
- C++20 subset only (no modules, coroutines, RTTI, exceptions in the public API).
- YAGNI: add nothing that a ported Wharfkit feature or an engine adapter does not need right now.
- Prefer one-liner implementations where they stay readable (expression-bodied functions, Result chaining, DK_TRY).
- Never use em dashes in code, comments, docs, or commit messages.
- Build and test after every item: cmake --build build && ctest --test-dir build.

Context limits:
- When the context nears its limit, write the exact state to PROGRESS.md (done, in flight file, next step), then run /compact and continue from PROGRESS.md.
- Never end a session mid-item without recording it.

This machine (Windows 11, VS 2022 Build Tools):
- Configure: cmake -S . -B build -G "Visual Studio 17 2022" -A x64
- Build:     cmake --build build --config Debug
- Test:      ctest --test-dir build -C Debug --output-on-failure
- The Bash tool is Git Bash; PowerShell is also available. Reference clones live in reference/ (gitignored); re-run scripts/fetch_reference.sh to refresh.
