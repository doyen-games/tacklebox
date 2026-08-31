# Progress

Rules: see CLAUDE.md. Tick an item only when it builds, its tests are green, and it is committed. Keep the "Current state" block accurate at all times; a fresh session resumes from it.

## Current state

- Phase: 8 complete. All blueprint phases done, and both deferred items have landed (strictExtensions decoding, antelope p2p module). Nothing tracked remains; future work is upstream re-syncs.
- In flight: nothing
- Notes: CancelToken deferred to protocol-esr. strictExtensions decoding ported (DecodeOptions on both static and dynamic decode; default synthesis with circular detection). K1 byte-parity vectors verified against elliptic via node (scratchpad/elliptest). miniz was replaced with vendored zlib 1.3.1 for byte parity with pako (fixture hashes + ESR URIs); see DIVERGENCES.md.

## Phase 0: Bootstrap

- [x] Repo skeleton, CMake, options, warnings-as-errors, `third_party/` vendoring, doctest smoke test.
- [x] `scripts/fetch_reference.sh`: `gh repo list wharfkit --limit 100 --json name` then shallow clone all into `reference/`, plus `greymass/buoy-client`. Classify every repo in `PORT_MANIFEST.md` (port / reference only / skip, with reason).
- [x] Copy the phase checklists into `PROGRESS.md`; create `DIVERGENCES.md` seeded from section 2; create `CLAUDE.md` from section 11.

## Phase 1: antelope core

- [x] `core/`: `Result`, `Error`, `json` helpers, `Bytes`, hex, base64, base58 (all check variants), sha256/sha512/ripemd160/hmac, CSPRNG shim. (base64 deferred to first call site)
- [x] Chain types: integers (incl. 128-bit, var ints, `toJSON` rules), `Float128`, `Name` + `_n`, `Asset`/`Symbol`/`SymbolCode`/`ExtendedAsset` + literals, time types, checksums, `BlockId`, `Blob`.
- [x] Crypto: K1 via libsecp256k1 with the elliptic-compatible nonce function; R1 via trezor-crypto; WA parse/verify; `PublicKey`, `PrivateKey`, `Signature` full method sets; key string formats. (WA verify deferred to serializer item; WA string parse/serialize done; K1 byte-parity proven vs elliptic)
- [x] `ABI` model and `resolveType`, `ABIEncoder`/`ABIDecoder`, builtins, `Serializer` (static, dynamic, `synthesize`, `stringify`), `DK_STRUCT`/`DK_FIELDS`/`DK_VARIANT`/`DK_TYPE_ALIAS`/`BinaryExtension`, `Action`, `Transaction`, `SignedTransaction`, `PackedTransaction`, authority types. (serializer.ts, webauthn.ts and the transaction/authority chain.ts cases ported; typestresser byte-parity green; strictExtensions deferred)
- [x] `APIClient`, `APIProvider`, `FetchProvider`, `CurlFetchProvider`, `MockFetchProvider`, `v1.chain` and `v1.history` with all `types.ts` structs.
- [x] All `antelope/test/tests/*.ts` ported and green against copied fixtures. (p2p.ts ported with the p2p/ module; bug-report.ts is an empty template; TS-runtime-only cases documented in DIVERGENCES.md)

## Phase 2: common, abicache, signing-request, protocol-esr

- [x] `common`: `Chains`, `ChainDefinition`, explorers, logos. (Vaulta present, answering open question 2; accountDataType becomes a template param on get_account, see DIVERGENCES.md)
- [x] `abicache`. (pending promise map becomes a mutex; upstream merge/partial logic ported)
- [x] `signing_request` with zlib and base64u; all its tests. (zlib is built in, the ZlibProvider option becomes a bool; ChainId subclasses Checksum256 so the checksum classes lost `final`; placeholder resolution walks decoded JSON; see DIVERGENCES.md. request.ts and misc.ts ported with exact esr:// URI, digest and IdentityProof vectors.)
- [x] `protocol_esr` + buoy send/receive over the transport interfaces; `CurlWebSocketProvider`; uuid v4; unit tests with a fake `WebSocketProvider`. (Includes sealed-messages with trezor Gladman AES, byte parity proven against @wharfkit/sealed-messages under node; `core/cancel.hpp` CancelToken; `transport/websocket_provider.hpp`; buoy Listener as a blocking receive loop with heartbeat acks and backoff reconnects; live send/receive against cb.anchor.link verified with DK_LIVE_TESTS. createIdentityRequest takes EsrLoginContext, the LoginContext slice, until the session kit lands.)

## Phase 3: session and plugins

- [x] `session`: kit, session, login, transact pipeline, plugin base classes, `UserInterface`, storage, translations, `ConsoleUserInterface`, `NullUserInterface`, `FileSessionStorage`, `MemorySessionStorage`. (session.ts/kit.ts/transact.ts/context.ts/utils.ts/wallet.ts/ui.ts/abi.ts/beforeSign.ts ported against the recorded jungle4 fixtures, send_transaction bodies byte-exact; ContractKit-dependent cases deferred to the contract kit; translations are type-level, the repo ships none)
- [x] `WalletPluginPrivateKey`, `WalletPluginMock`, `TransactPluginMock`; session test suite green. (mocks live in tests/util like upstream @wharfkit/mock-data)
- [x] `TransactPluginResourceProvider`, `Cosigner`, `ExplorerLink`, `FinalityChecker`, `FinalityCallback`, `Autocorrect`, msig plugin; their tests. (msig = @wharfkit/msigs MsigsClient; some fixtures re-keyed for the current wire format where the recordings predate it, signatures proven identical; see DIVERGENCES.md)
- [x] Login and account-creation plugin bases; Greymass and Jungle account-creation plugins. (bases live in the session module; AccountCreationPluginAnchor with the popup flow as an openDialog hook; AccountCreationPluginJungle4 faucet flow)
- [x] `examples/transfer_privatekey` runs against Jungle 4. (verified live: tx 03e17ab81cac160fc7ed7d59b6f58a38be4a6f397189a1e0f90d0ad4117177b0 broadcast from wharfkit1111)

## Phase 4: contract, account, resources, token

- [x] `contract` + tests. (kit.ts/contract.ts/table.ts/types.ts/utils.ts; rows are json, request bodies key-order-matched to the recorded fixtures; the two deferred session transact cases now ported)
- [x] `resources` + tests (numeric parity). (pulled forward for the resource provider plugin; RAM/REX/PowerUp exact values on eos/jungle/wax fixtures; UInt128/Int128 gained multiply/divide with the upstream rounding modes)
- [x] `token` + tests. (Token + embedded system.token contract; balance fixtures re-keyed under current-antelope bodies)
- [x] `account` + tests. (AccountKit<Data>/Account<Data> templates for chain-specific account objects; Permission/Resource; embedded eosio system contract; broadcast fixtures byte-matched)
- [x] Any additional library packages found in Phase 0. (actionstream: blocking pull client; atomicassets: endpoints for assets/market/tools v1+v2, objects, kits, with the three contract modules produced by dkgen)

## Phase 5: wallets

- [x] `WalletPluginAnchor` with qrcodegen util; `examples/anchor_login` (console prints QR as text). (native + web transports over openLink hook; interactive mode chooser degrades to option-driven selection; login-and-sign test stubs buoy with a fake WebSocketProvider)
- [x] `WalletPluginCloudWallet` protocol layer + `WebViewBridge` interface. (popup/postMessage exchange behind WebViewBridge; validateModifications with value-equality auth check; the upstream commented-out login-and-sign test runs here against a scripted bridge)
- [x] `WalletPluginCleos`. (afterSign hook prompts the cleos push command; upstream commented-out test runs here with a prompt-recording UI)

## Phase 6: dkgen

- [x] Generator, offline `--abi` mode, golden-output tests for `eosio.token`, `eosio`, `atomicassets`. (tools/dkgen emits a self-contained header: abiBlob/abi(), Types with DK_STRUCT/DK_VARIANT in dependency order, Contract subclass with typed actions and table accessors; --abi accepts json or a base64 blob; checked-in golden headers compile in the test binary and encode byte-identically to the ABI serializer)

## Phase 7: engine adapters

- [x] Unreal plugin, sample map with login + transfer. (adapters/unreal/Dwarfkit: FHttp/FWebSockets providers, FileSessionStorage under ProjectSaved, Blueprint UDwarfkitUI + game-thread marshalling, UDwarfkitSubsystem, Login/Restore/Transact async nodes, ADwarfkitSampleActor drives the sample map; source-only, compiled inside a UE 5.4+ project against the prebuilt lib)
- [x] Godot GDExtension, sample scene with login + transfer. (adapters/godot: HTTPClient/WebSocketPeer providers polled off-thread, DkSessionKit/DkSession RefCounted wrappers with signals, DkUserInterface with call_deferred + Semaphore, json<->Variant, demo login_transfer.gd; source-only, built with scons + godot-cpp 4.3+)

## Phase 8: packaging and docs

- [x] `find_package(Dwarfkit)` install, versioning, README with the TS-to-C++ parity matrix, migration notes, `DIVERGENCES.md` final pass. (hand-rolled package config over the installed archives, verified by building and running an out-of-tree consumer against the installed prefix; README rewritten with the parity matrix and migration notes; DIVERGENCES stale rows corrected and packaging section added)

## Session log

- 2026-08-26: session 1 started. Environment: Windows 11, VS 2022 Build Tools 17.14, CMake 4.3.3, Ninja 1.13, MinGW g++ 16.1, git 2.51, gh authed. Canonical build: VS generator x64.
