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

## Branding checklist (when the logo pack lands)

- `CPACK_NSIS_MUI_ICON` / `CPACK_NSIS_MUI_UNIICON` - `.ico`
- `CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP` - 164x314 BMP
- `CPACK_NSIS_MUI_HEADERIMAGE_BITMAP` - 150x57 BMP
- Window/taskbar icon (SDL_SetWindowIcon) + the About card wordmark
