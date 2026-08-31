# Contributing to Dwarfkit

Thanks for your interest in improving Dwarfkit.

## What this project is

Dwarfkit is a 1:1 native C++20 port of the [Wharfkit](https://github.com/wharfkit)
suite. That shapes every contribution: the source of truth is the pinned
upstream TypeScript in `reference/` (fetched by `scripts/fetch_reference.sh`,
pinned in [PORT_MANIFEST.md](PORT_MANIFEST.md)), not anyone's memory of how
Wharfkit behaves. Fixes and features should match upstream behavior byte for
byte unless C++ or an engine forces a deviation, and every intentional
deviation gets a one-line entry in [DIVERGENCES.md](DIVERGENCES.md).

## Building and testing

```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Any toolchain with C++20 support should work; CI parity is currently verified
with MSVC (VS 2022) on Windows x64. Run the full test suite before opening a
pull request. Every change lands with its tests green.

## Porting or fixing a module

1. Read the TypeScript source in `reference/<repo>/src/` first.
2. Keep Wharfkit's names: packages, classes, methods, options, hooks.
3. Port the matching test file and its recorded fixtures, and make it green.
   Fixtures are keyed by a hash of the exact request; do not hand-edit
   recorded responses. If your (correct) request body differs from an old
   recording, re-key the fixture and note why in the commit.
4. Record any intentional deviation in DIVERGENCES.md with a one-line reason.
5. Update PROGRESS.md and PORT_MANIFEST.md if the module status changed.

## Style

- C++20 subset only: no modules, no coroutines, no RTTI, and no exceptions in
  the public API. Fallible paths return `Result<T>` and chain with `DK_TRY`.
- YAGNI: add nothing that a ported Wharfkit feature or an engine adapter does
  not need right now.
- Prefer one-liner implementations where they stay readable.
- Match the surrounding code's comment density and idiom.
- Never use em dashes in code, comments, docs, or commit messages.

## Commits and pull requests

- One logical change per commit, with the build and tests green at each one.
- Fill in the pull request template; it mirrors the porting checklist above.
- For behavior questions, link the upstream file and line in `reference/`.

## Reporting bugs

Use the issue templates. For anything security sensitive (key handling,
signing, transaction encoding), follow [SECURITY.md](SECURITY.md) instead of
opening a public issue.
