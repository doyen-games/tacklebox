# TackleBox on iOS and Android

One SDL3 shell drives every platform. The UI adapts by form factor, not by
build: phones get a bottom tab bar (Dashboard / Transfer / Explore / Assets +
a "More" sheet for the other nine pages) and full-screen sheet modals; tablets
get an icon navigation rail; desktops keep the full sidebar. Touch builds grow
every hit target, drive the OS keyboard from focused fields, and inset the
chrome by the safe areas (notches, home indicators).

Every layout is exercisable on a desktop today:

```
tacklebox --phone --touch     # phone chrome in a 400x780 window
tacklebox --tablet            # rail chrome
tacklebox --desktop           # force the sidebar even in a narrow window
```

## Honest status

- The desktop SDL3 shell (Windows/Linux/macOS) is built and verified.
- The phone/tablet chromes are built and verified on desktop via the flags
  above.
- The iOS and Android projects below are complete scaffolds wired to the same
  CMake build, but **have not been compiled here**: they need Xcode on a Mac
  and the Android SDK/NDK respectively. Expect first-build friction (NDK
  versions, gradle plugin drift), not architectural surprises.
- Mobile-specific behavior already in the code: `TB_MOBILE` builds use an
  OpenGL ES 3.0 context, app-sandbox storage (`SDL_GetPrefPath`), lock-on-
  background (Settings > Security, default on), OS keyboard show/hide from
  focused text fields, and safe-area insets.

## Android

Prereqs: Android Studio (or CLI SDK), NDK r26+, CMake 3.24+ in the SDK.

```
cd platform/android
./gradlew assembleDebug
```

What the build does:

- Gradle invokes the repo's root CMakeLists with `-DTB_MOBILE=ON`; the app
  builds as `libmain.so`, which SDL3's activity loads.
- SDL3's Java activity classes are sourced directly from
  `vendor/sdl/android-project` (nothing copied).
- curl gets TLS from mbedTLS 3.6 (FetchContent'd automatically for Android
  builds); certificate verification uses mbedTLS' defaults - review
  `CURL_CA_BUNDLE` handling before shipping.
- `TackleBoxActivity` sets `FLAG_SECURE` (no screenshots/recents preview) and
  the manifest sets `allowBackup=false` (the sealed vault leaves the device
  only via in-app export).

## iOS / iPadOS

Prereqs: a Mac with Xcode 15+, CMake 3.24+.

```
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DTB_MOBILE=ON -DTB_BUILD_TESTS=OFF \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<your team id>
cmake --build build-ios --config Release -- -allowProvisioningUpdates
```

- SDL3 provides the UIKit application plumbing; `SDL_main.h` supplies the
  entry point.
- curl builds against Secure Transport (`CURL_USE_SECTRANSP`), so certificate
  trust comes from the OS keychain.
- `platform/ios/Info.plist.in` declares iPhone + iPad orientations and keeps
  App Transport Security strict (the app is https-only anyway).
- iPad renders the tablet chrome in full-screen and the desktop chrome in
  large Split View widths - the form factor tracks the actual window size.

## Mobile security notes

- **Lock on background** (default on): iOS/Android may snapshot the app and
  keep it suspended for days; TackleBox seals the vault the moment it leaves
  the foreground. Turn it off only on a device you trust completely.
- The scrypt vault parameters are the same as desktop (N=2^15: ~32 MB,
  comfortably within mobile budgets).
- Biometric unlock (Face ID / BiometricPrompt) is intentionally absent for
  now: doing it properly means wrapping the vault key with a
  Keychain/Keystore-resident key, not gating a cached password behind a
  fingerprint. It has a clean seam (`Vault::unlock`) when it lands.
- Autopilot schedules only run while the app is foreground and unlocked; there
  is no background execution on mobile, by design.
