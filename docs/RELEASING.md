# Releasing TackleBox

## Cutting a release

1. Bump `project(TackleBox VERSION x.y.z ...)` in `CMakeLists.txt` (the one
   source of truth - sidebar, About, updater and packages all read it).
2. Commit, then tag and push:

   ```
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```

3. The `release` workflow verifies the tag matches `PROJECT_VERSION`, builds,
   runs the tests, packages the NSIS installer + portable ZIP, signs them if a
   certificate is configured (below), writes `SHA256SUMS.txt`, and attaches
   everything to a **draft** GitHub release.
4. Review and publish the draft. Publishing is the trigger for running
   wallets: the in-app update check reads `/releases/latest` and starts
   offering the new version the moment the release is public.

## Packaging gotchas (learned the hard way in v0.3.0)

The v0.3.0 installer was 378 KB against a 7.5 MB ZIP: it contained no
`tacklebox.exe`. Two CPack traps, both now guarded in `CMakeLists.txt`:

- In `install(TARGETS ...)`, `COMPONENT` applies to the artifact-kind clause
  it follows. Trailing it after `BUNDLE DESTINATION .` put only the (absent)
  bundle in `app`; the RUNTIME exe fell into "Unspecified", which the
  component-aware NSIS generator never packages. `COMPONENT app` leads the
  rule so it covers every kind.
- Never set `CPACK_COMPONENTS_ALL`: it switches NSIS into component mode,
  where the generator overwrites `CPACK_NSIS_DEFINES` with its own download
  flags and our `RequestExecutionLevel user` + version block vanish (the
  wizard then demands admin). The `app` filter lives in
  `CPACK_INSTALL_CMAKE_PROJECTS` instead, and the defines use single-quoted
  NSIS strings (a double quote inside breaks the generated
  `CPackConfig.cmake`).

Sanity checks before trusting a release build:

- The installer must be a few MB (LZMA of the exe), never a fraction of the
  ZIP. Compare the two assets' sizes on the release page.
- Stage locally without NSIS: point `cpack` at a stub `makensis` that only
  answers `/VERSION` (`cpack -G NSIS -D CPACK_NSIS_EXECUTABLE=stub.exe`),
  then inspect `build/_CPack_Packages/win64/NSIS/<pkg>/` for the exe and
  `project.nsi` for `RequestExecutionLevel user` + the `VIAddVersionKey`
  block (they sit right after the template's own `RequestExecutionLevel
  admin`, which they override).
- The workflow builds the tag's commit, not `main`: a tag cut before a
  workflow fix runs the old workflow (that was the missing-NSIS run).

## Code signing (Windows)

Unsigned installers trip SmartScreen ("unrecognized app"), which loses most
downloads. The workflow signs both `tacklebox.exe` and the installer when two
repository secrets exist; with no secrets it ships unsigned and prints a
notice.

- `WINDOWS_CERT_PFX_B64` - your Authenticode certificate + key as a base64
  encoded PFX: `base64 -w0 signing.pfx`
- `WINDOWS_CERT_PASSWORD` - the PFX password

Any OV code-signing certificate works; timestamping uses DigiCert's public
server so signatures outlive the certificate. If you move to Azure Trusted
Signing later, replace the two `Sign the ...` steps in
`.github/workflows/release.yml` with the `azure/trusted-signing-action`.

## Fuzzing

CI smoke-fuzzes the attacker-facing parsers (ESR uris, vault network JSON,
sealed link messages) for ~25s each per push. To fuzz longer locally
(needs clang):

```
cmake -S . -B build-fuzz -G Ninja -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ -DTB_BUILD_FUZZERS=ON
cmake --build build-fuzz
./build-fuzz/fuzz_esr corpus_esr/ -max_total_time=3600
```

Keep any crash artifacts (`crash-*`) - they reproduce the input exactly.

## Branding

Done: `CPACK_NSIS_MUI_ICON` / `MUI_UNIICON` (`assets/brand/tacklebox.ico`),
the executable's icon + version resource (`assets/brand/tacklebox.rc`), the
runtime window/taskbar icon (`src/ui/brand_icon.hpp`), and the in-app mark
(`drawTackleboxMark`). Still stock:

- `CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP` - 164x314 BMP
- `CPACK_NSIS_MUI_HEADERIMAGE_BITMAP` - 150x57 BMP
