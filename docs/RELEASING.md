# Releasing TackleBox

## Cutting a release

1. Bump `project(TackleBox VERSION x.y.z ...)` in `CMakeLists.txt` (the one
   source of truth - sidebar, About, updater and packages all read it).
2. Commit, then tag and push:

   ```
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```

3. The `release` workflow verifies the tag matches `PROJECT_VERSION`, then
   builds, tests and packages on three runners in parallel, signs what the
   configured secrets allow (below), and a final `publish` job writes one
   `SHA256SUMS.txt` and attaches everything to a **draft** GitHub release:

   | Platform | Assets |
   |---|---|
   | Windows | `tacklebox-<v>-Windows.exe` (per-user NSIS wizard), `tacklebox-<v>-Windows.zip` (portable) |
   | Linux | `TackleBox-<v>-x86_64.AppImage` (+ `.zsync`), `tacklebox_<v>_amd64.deb`, `tacklebox-<v>-Linux-x86_64.tar.gz` |
   | macOS | `tacklebox-<v>-macOS.dmg` (universal arm64 + x86_64, drag to Applications) |

   `ci.yml` builds the very same packages on every push (unsigned) and keeps
   them as workflow artifacts for two weeks, so a packaging fault surfaces
   before a tag does.
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
- `ci.yml` now compiles the installer on every push and keeps it as the
  `tacklebox-windows-installer` artifact; both workflows fail when the
  installer is under 3 MB or when the generated `project.nsi` lacks the
  vendored template's marker comment.

## The installer template is vendored

`cmake/Internal/CPack/NSIS.template.in` is CMake 4.3.3's stock template with
three changes (its header lists them; `NSIS.template.in.orig` is the pristine
copy, so `diff` shows exactly what we own):

1. **Per-user, always.** `RequestExecutionLevel user` and
   `SetShellVarContext current` in both `.onInit` and `un.onInit`. The stock
   template asks `UserInfo::GetAccountType` and switches an admin token to
   all-users mode: files under `C:\ProgramData\Programs`, registration under
   `HKLM`, all-users Start menu. That is what the (elevated) v0.3.0 installer
   did, and it is why an "as administrator" run of any stock installer would
   do it again.
2. **Default folder** is `CPACK_NSIS_INSTALL_ROOT\TackleBox` for everyone.
   Stock sends a non-admin token to `My Documents\TackleBox`.
3. **Upgrades never abort.** Stock `.onInit` runs the previous
   `Uninstall.exe` with `ExecWait` and shows "Uninstall failed." + Abort on
   any launch error. An uninstaller left by an elevated install carries an
   admin manifest, and CreateProcess from an un-elevated process fails with
   ERROR_ELEVATION_REQUIRED, so every v0.3.0 user was stuck. The vendored
   `.onInit` looks at both `HKLM` (elevated, launched through the shell's
   `runas` verb: one UAC prompt) and `HKCU` (per-user, run inline), waits
   for each (`_?=`), deletes the leftover `Uninstall.exe` that an in-place
   uninstaller cannot remove itself, and on failure only tells the user the
   old entry stays listed under Installed apps.

`CPACK_MODULE_PATH` lists both `cmake` and `cmake/Internal/CPack` because
CMake versions differ in the name they look the template up by. Re-vendoring
for a newer CMake: copy the new stock file over `.orig`, re-apply the diff.

No `makensis` on the dev box: stage with the stub (above) and read the
generated `project.nsi`; the CI artifact is the compiled proof.

## Linux packaging

Three artifacts come out of one GNU-layout install tree (`bin/tacklebox`,
`share/applications/tacklebox.desktop`, hicolor icons, `share/doc`):

- **AppImage** - `packaging/linux/appimage.sh <build-dir> [out-dir]` installs
  the `app` component into an AppDir and runs linuxdeploy, which bundles the
  shared libraries the binary needs (libcurl and its tail) and leaves glibc,
  X11, Wayland and Mesa to the host. The glibc floor is the builder's, which
  is why both workflows build on `ubuntu-22.04` (glibc 2.35; GCC 13 from the
  `ubuntu-toolchain-r/test` PPA, since 22.04's GCC 12 cannot compile
  dwarfkit) and the binary links libstdc++/libgcc statically. `LDAI_UPDATE_INFORMATION` points
  AppImageUpdate at this repository's latest release.
- **.deb** - CPack's DEB generator with `dpkg-shlibdeps` computing `Depends`.
- **tarball** - the same tree, for any distro.

Two things the binary does for itself on Linux:

- **URL schemes.** A packaged install ships `tacklebox.desktop` with
  `MimeType=x-scheme-handler/tacklebox;x-scheme-handler/esr;`. A portable
  copy (AppImage, tarball in `$HOME`) writes the same entry to
  `~/.local/share/applications` with its own path in `Exec` (`$APPIMAGE` for
  an AppImage), drops the 256 px mark into the hicolor theme, and refreshes
  the launcher cache. Either way `~/.config/mimeapps.list` gets
  `x-scheme-handler/tacklebox` pointed at us, and `esr` only when no other
  handler is set (`deeplink::registerSchemes`; the pure halves are tested in
  `tests/test_deeplink_registration.cpp`). Second launches forward their uri
  over a unix socket in `$XDG_RUNTIME_DIR`, scoped by data dir like the
  Windows pipe.
- **libcurl.** Ubuntu 22.04's libcurl (7.81) predates the WebSocket API
  dwarfkit's transport uses, so the builders install no libcurl and dwarfkit
  builds the release pinned in `CMakeLists.txt` (`FetchContent_Declare(curl
  URL ... URL_HASH ...)`, declared ahead of dwarfkit's own so it wins) as a
  static library against the image's shared OpenSSL 3 and zlib. The same
  pin serves the Windows build, which has no system libcurl either.
- **CA bundle.** That libcurl carries Ubuntu's compiled-in CA path; on
  Fedora, SUSE or Alpine the file does not exist and every https call would
  fail. `src/platform/linux_tls.cpp` wraps `curl_easy_init` at link time
  (`-Wl,--wrap`) and points each handle at the first bundle that exists
  (`core/ca_bundle.hpp`); curl's own `CURL_CA_FALLBACK` is on as a second
  net. The vendored transport stays untouched.

Local check without a Linux box: the `linux` job in `ci.yml` is the recipe
(`apt-get` line, configure flags, the two `cpack`/`appimage.sh` steps); an
Ubuntu 22.04 container running those commands against the checkout produces
the same artifacts.

## macOS packaging

`TackleBox.app` is built universal (`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3` - pass both, the plist's
`LSMinimumSystemVersion` is filled from the latter; 13.3 is where libc++
gained the floating-point `std::to_chars` dwarfkit's serializer uses) and CPack's DragNDrop
generator turns it into a drag-to-Applications disk image. The plist
template `packaging/macos/Info.plist.in` declares the `tacklebox:` and `esr:`
schemes; LaunchServices delivers an opened url to the running app as an Apple
event, SDL surfaces it as `SDL_EVENT_DROP_FILE`, and `handleDrop` in
`main.cpp` hands anything that parses as a deep link to the controller. The
icon is `assets/brand/tacklebox.icns` (generated from the 256 px mark; the
512/1024 slots wait for a larger render of the SVG).

Without a Developer ID signature Gatekeeper refuses the app on first launch
("cannot be opened because the developer cannot be verified"); the workaround
is right-click > Open once, or `xattr -d com.apple.quarantine
/Applications/TackleBox.app`. Signing (next section) removes that.

## Code signing (Windows)

"Unknown publisher" in the SmartScreen / UAC / Open File dialogs has one
cause: the binary carries no Authenticode signature. No amount of metadata
changes it (`CPACK_PACKAGE_VENDOR`, the `.rc` company name and the
installer's version block only feed Properties > Details and Installed
apps). The workflow signs both `tacklebox.exe` and the installer when one of
two sets of secrets exists; without them it ships unsigned and prints a
notice. "Run workflow" on the `release` workflow is a dry run that builds
and signs without touching a release - use it to prove new secrets.

**Route 1 - Azure Artifact Signing (recommended).** Microsoft's managed
signing service (renamed from Trusted Signing in 2026): the certificate
names the publisher exactly as validated, keys stay in Microsoft's HSM
(FIPS 140-3 L3), certificates are three-day and auto-renewed (signatures
outlive them through the `timestamp.acs.microsoft.com` timestamp), Basic
tier is a small monthly fee billed in full each month.

Who the publisher can be:

- *Organization* validation puts the legal entity's name on the certificate
  (`CN=Doyen Games`). It requires a registered business (business
  identifier), a website on a domain the business owns, a monitored mailbox
  on that domain, and a personal ID check of the representative. Available
  to organisations in the US, Canada, EU, UK, Australia, New Zealand, Japan,
  South Korea, Singapore, Switzerland, Norway and Israel. 1-20 business
  days; Microsoft may ask for registration documents (three upload
  attempts).
- *Individual* validation puts the developer's own legal name on the
  certificate (city/state/country too), sourced from the Azure billing
  account, which must match the government ID. US and Canada only.
  Verification is done on a phone (Microsoft Authenticator Verified ID via
  AU10TIX) and completes in minutes.
- The CN cannot be customised (CA/Browser Forum rule), and free/trial/
  sponsored Azure subscriptions are refused: it needs pay-as-you-go.

Setup, once:

1. Azure portal > Subscriptions > your subscription > Resource providers >
   `Microsoft.CodeSigning` > Register.
2. Search "Artifact Signing Accounts" > Create: new resource group, a
   globally unique account name (3-24 alphanumerics), a region (East US ->
   endpoint `https://eus.codesigning.azure.net`, West Europe ->
   `https://weu.codesigning.azure.net`; the full table is in the quickstart),
   pricing tier Basic.
3. On the account > Access control (IAM) > Add role assignment > **Artifact
   Signing Identity Verifier** > yourself (you also need Reader on the
   subscription).
4. Account > Identity validations > New identity > Public > Organization
   (or Individual): fill it exactly as the legal records read, create,
   then complete the personal verification link that arrives by email
   (expires in seven days) and wait for **Completed**.
5. Account > Certificate profiles > Create > **Public Trust** > name it
   (5-100 chars), pick the completed validation under "Verified CN and O",
   check the certificate subject preview, create.
6. Microsoft Entra ID > App registrations > New registration (single
   tenant) > note the Application (client) ID and Directory (tenant) ID >
   Certificates & secrets > New client secret > copy the value now.
7. Account (or the certificate profile, for the narrowest scope) > Access
   control (IAM) > Add role assignment > **Artifact Signing Certificate
   Profile Signer** > Members: "User, group, or service principal" > the app
   registration.
8. Repository secrets: `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`,
   `AZURE_CLIENT_SECRET`, `ARTIFACT_SIGNING_ENDPOINT` (the region URL),
   `ARTIFACT_SIGNING_ACCOUNT` (account name), `ARTIFACT_SIGNING_PROFILE`
   (certificate profile name).
9. Actions > release > Run workflow on `main`, download the
   `release-windows` artifact, and check Properties > Digital Signatures on
   the installer. Then tag.

Renew the identity validation when Azure's reminders start (60 days before
it expires); an expired validation stops certificate renewal and therefore
signing. SmartScreen's "Windows protected your PC" interstitial is separate
from the publisher line: it fades as download reputation accrues to the
publisher, and a signed file can be submitted to Microsoft Security
Intelligence for review.

**Route 2 - your own certificate as a PFX.** `WINDOWS_CERT_PFX_B64` (base64
of the .pfx, `base64 -w0 signing.pfx`) + `WINDOWS_CERT_PASSWORD`. Since June
2023 CAs issue OV/EV certificates only on hardware tokens or in cloud HSMs,
so a fresh certificate cannot be exported to a PFX; this route fits a
certificate issued before that or a CA-hosted signer with its own action.

For an open-source project the SignPath Foundation signs releases for free,
but the publisher line then reads "SignPath Foundation", not Doyen Games.

## Code signing (macOS)

Needs an Apple Developer Program membership (US$99/year) and a *Developer ID
Application* certificate. Export it with its key as a `.p12`, then set:

- `APPLE_CERTIFICATE_P12_B64` (base64 of the .p12) and
  `APPLE_CERTIFICATE_PASSWORD`
- `APPLE_SIGNING_IDENTITY` - the certificate's common name, e.g.
  `Developer ID Application: Doyen Games (TEAMID)`
- `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` (an app-specific
  password from appleid.apple.com) for `notarytool`

The workflow then signs the bundle with the hardened runtime, builds the
disk image, signs it, submits it for notarization, waits, and staples the
ticket, so the app opens on any Mac without warnings.

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
