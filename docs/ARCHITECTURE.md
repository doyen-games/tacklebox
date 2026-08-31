# TackleBox architecture

## The stack

```
+--------------------------------------------------------------+
|  ui/         Dear ImGui views, theme, fx, widgets, textures  |   main thread
+--------------------------------------------------------------+
|  app/        Controller + AppState + dwarfkit bridge         |   both
+--------------------------------------------------------------+
|  guard/      whitelist engine + risk analyzer (pure logic)   |   any thread
|  vault/      scrypt + sealed box + vault document            |   vaultMutex
|  chain/      per-network services (RPC, AA API, probes)      |   workers
+--------------------------------------------------------------+
|  dwarfkit    chain types, serializer, crypto, session kit,   |   workers
|              ESR, contract/token/account kits, curl fetch    |
+--------------------------------------------------------------+
```

## Threading model

Dwarfkit is blocking by design (see its DIVERGENCES.md), so:

- The **main thread** runs GLFW + ImGui at vsync and owns `AppState`. It never
  performs network or KDF work.
- A small **worker pool** (`TaskRunner`, 2-4 threads) runs every dwarfkit
  call, scrypt derivation, and signing. Results return via
  `TaskRunner::postMain`, drained once per frame.
- The **vault** is shared between UI intent and worker signing, guarded by one
  mutex inside the Controller. The UI never touches `Vault` directly: it reads
  an immutable `VaultSnapshot` (secrets blanked) refreshed after any mutation.
- **PromptBroker** lets a worker block on a human decision. The wallet
  plugin's `sign()` publishes a `SignPrompt` payload and waits on a condition
  variable; the UI renders the modal and resolves it. The same mechanism
  serves dwarfkit transact-plugin prompts (e.g. resource-provider fees).

## The signing path

```
UI intent (transfer / contract form / ESR paste)
  -> Controller::transactAsync                    [worker]
     -> dwarfkit Session::transact
        - resolves ABIs (shared ABICache), TAPOS, expiration
        - beforeSign hooks
        - VaultWalletPlugin::sign(resolved, ctx)
            -> Controller::guardedSign            [same worker]
               - decode actions -> guard inputs
               - get_raw_abi per contract (fresh) -> hash check
               - guard::evaluate (whitelist) + guard::analyze (risk)
               - persist stale pins immediately
               - auto-sign fast path or PromptBroker wait -> signing modal
               - PrivateKey::signDigest(resolved.signingDigest())
               - audit log append
        - broadcast, afterBroadcast hooks
  -> completion posted to main: toasts, refreshes
```

Because the guard lives inside the WalletPlugin, every dwarfkit signing route
passes through it by construction - hand-built transfers, contract forms,
system actions (RAM/stake/vote/deploy), msig operations, pasted ESRs, pushed
link-session requests, and autopilot schedules all converge on guardedSign.

Two flow variants ride the same path:

- **Autopilot** sets a thread-local flag around its transact call; guardedSign
  then refuses to prompt, so only the auto-sign fast path can produce a
  signature (see docs/SECURITY.md).
- **Link sessions** (app/link.cpp) run one listener thread per session pumping
  dwarfkit's buoy Listener; unsealed requests are handed to the controller,
  which transacts them with the session's account and answers the dapp's
  callback with the signatures.

## Vault file format

JSON envelope, encrypted payload; see docs/SECURITY.md for the cryptography.
The payload holds keys, accounts, networks (with endpoints + Atomic API URL),
whitelist rules, security prefs, and the audit log. Saves are atomic with a
rotating `.bak`; import/export moves the sealed envelope verbatim.

## Whitelist engine

`guard/` is pure logic with no I/O so the test suite can drive it hard:

- `rules.hpp` - rule + constraint model, JSON round trip, matching semantics
  (`jsonEquiv` bridges the serializer's string-form big integers, asset ranges
  compare amounts only under matching symbol+precision).
- `engine.hpp` - `evaluate(rules, chain, signer, actions) -> verdicts`,
  specific-action rules outrank wildcards, the weakest action decides the
  transaction verdict, stale pins are reported for persistence.
- `risk.hpp` - static flags independent of the whitelist.

## Platform shell and form factors

`src/main.cpp` is one SDL3 shell for all five targets. Desktop builds create a
GL 3.2 core context; `TB_MOBILE` builds (iOS/Android, and the CMake option for
testing) use GLES 3.0, fullscreen, app-sandbox storage via `SDL_GetPrefPath`,
and honor lifecycle events - entering background seals the vault when the
lock-on-background pref is on. Text input drives the OS keyboard from
`io.WantTextInput`; `SDL_GetWindowSafeArea` insets the chrome.

`ui/layout.*` computes a FormFactor (Phone <640 / Tablet <1000 / Desktop) from
the viewport each frame, overridable with `--phone/--tablet/--desktop` and
`--touch`. The shell renders one of three chromes (bottom tab bar + More
sheet / icon rail / sidebar); `beginAdaptiveModal` turns every modal into a
full-screen sheet on phones; `pairWidth`/`maybeSameLine` stack side-by-side
cards; wide tables slim down or become card lists on phones. Core code never
sees the windowing layer (clipboard goes through `core/clipboard.hpp`
function pointers the shell installs).

## UI conventions

- One palette (`ui/theme.hpp`), two typefaces: Rajdhani for structure, Share
  Tech Mono for data. All custom widgets live in `ui/widgets.*`; drawing
  helpers and the icon set in `ui/fx.*` (pure ImDrawList paths, no textures).
- Views are one translation unit each under `ui/views/`, all with the
  signature `void drawX(AppState&, Controller&)`. View-local input buffers are
  function-local statics; cross-view state lives in `AppState`.
- Remote NFT media flows through `ui/texcache.*`: worker fetch (https-only,
  size-capped) -> stb_image decode -> GL upload on main -> LRU eviction.
- The app idles (`glfwWaitEventsTimeout`) when unfocused and quiet.

## Vendored dependencies

Snapshots under `vendor/`, provenance in `vendor/dwarfkit/VENDORED_COMMIT.txt`.
Local patches to dwarfkit are limited to compiler-warning hygiene for GCC 14+
(documented in that file). Dwarfkit itself FetchContent-pins libsecp256k1
v0.6.0 and libcurl 8.10.1 when the system provides neither.
