# Divergences

Every intentional deviation from Wharfkit, one line of reason each. Anything not listed here is a bug in the port.

## Fixed by BLUEPRINT.md section 2

| Wharfkit | Dwarfkit | Why |
|---|---|---|
| `throw` | `Result<T>` return values, no exceptions in the public API | Unreal and godot-cpp build without exceptions by default |
| `async` / `Promise` | Blocking functions; caller runs them on a worker thread | Engines own scheduling; a C++ async layer would match neither engine |
| `Cancelable<T>` | `CancelToken` argument | Same semantics without a promise type |
| Decorators (`@Struct.type`, `@Struct.field`) | `DK_STRUCT` / `DK_FIELDS` macros plus compile-time reflection | No decorators in C++ |
| Plain JS objects | `dk::json` (nlohmann `ordered_json`) | Needs an untyped value type; insertion order kept for stringify parity |
| `fetch` | `FetchProvider` interface, `CurlFetchProvider` default | Engines supply their own HTTP |
| WebSocket (buoy) | `WebSocketProvider` interface, libcurl default | Same reason |
| `BrowserLocalStorage` | `FileSessionStorage`, `MemorySessionStorage` | No browser |
| Class instances passed by reference | `std::shared_ptr` for kits, plugins, providers, UI; value types for chain types | C++ ownership |
| String literals for names | `"eosio.token"_n` also accepted | One-liner ergonomics, matches CDT |
| pako (zlib) | Vendored zlib 1.3.1 (the blueprint said miniz; see the porting table) | pako emits byte-identical streams to zlib, so packed transactions and ESR URIs stay byte-compatible |
| `web-renderer` | Not ported | Engines render their own UI against the `UserInterface` contract |
| Scatter, Wombat, TokenPocket, MetaMask wallet plugins, `protocol-scatter` | Not ported | Browser `window.*` injection protocols; impossible without a browser |
| `WalletPluginCloudWallet` popup + `postMessage` | Protocol layer plus `WebViewBridge` interface; adapters supply the web view | No popups in a game engine |

## Found while porting

(append here, newest last)

| Wharfkit | Dwarfkit | Why |
|---|---|---|
| TS type guards (`Bytes.isBytes`, `isInstanceOf`, `arrayEquals`) | Dropped | C++ overload resolution and `operator==` replace runtime type sniffing |
| `Base58.DecodingError` subclass with `code`/`info` fields | `Error{kind: Invalid}` with `details["code"]` = "E_CHECKSUM"/"E_INVALID" plus the info pairs (byte arrays as hex strings) | Single error type across the library |
| `Bytes` shares the underlying buffer between instances | Value semantics, always copies | C++ vectors; upstream even warns about the sharing |
| `Int8..UInt64` wrapper classes with C++11-emulating operators | Native `int8_t..uint64_t`; `Int128`/`UInt128`/`VarInt`/`VarUInt` remain real types; `intToJSON` keeps the >32-bit-becomes-string rule | The wrappers reimplement C++ in JS; the port runs on the original |
| `Float32`/`Float64` wrapper classes | Native `float`/`double`; toString/toJSON rules live in the serializer builtins | BLUEPRINT.md 5.6 type map |
| `Asset.value` getter throws when units exceed 53 bits | `Asset::value()` returns the full double; the strict 53-bit error stays on `Symbol::convertUnits` | C++ doubles have no 53-bit API cliff; parity kept where the upstream test asserts |
| `toFixed` rounds decimal ties toward positive infinity | `Symbol::convertFloat` uses printf rounding (ties to even) | Differs only when value*10^precision is exactly representable at a .5 tie |
| `TimePoint.equals(badString)` throws | Returns false | equals returning Result would break comparison ergonomics |
| `_n` name literal accepts what `Name.from` accepts (silent mangling) | `_n`, `_asset`, `_symbol` are consteval and reject invalid literals at compile time | Compile-time validation is free and matches CDT |
| elliptic for K1 and R1 curves | libsecp256k1 for K1 (custom nonce reproduces elliptic's HMAC-DRBG with pers=[attempt]; byte-parity proven), trezor-crypto nist256p1 for R1 | No elliptic in C++; K1 parity is asserted, R1 tests are round-trip only |
| `PrivateKey` mutable `.data` reassignment (a test sets `k.data = Bytes.random(31)`) | Not supported; construct via `PrivateKey::make` which enforces the 32-byte invariant | That test asserts a throw C++ gives at construction |
| `@Struct.type` decorators, `Struct.from(value)` | `DK_STRUCT`/`DK_FIELDS` on plain aggregates; construct with designated initializers or `dwarfkit::structFrom<T>(json)` | No decorators; a member `from` would collide with fields named `from` (transfer) and break aggregate init |
| `Struct`/`Variant` runtime classes built from `abiFields` arrays, `customTypes` decoding, encoder/decoder `metadata` | Not ported; the typed path is compile-time (`DK_` macros + `abi_traits`), the dynamic path is json + ABI | TS runtime class machinery has no C++ counterpart; revisit only if a ported call site needs it |
| Untyped JS-object encode (`Serializer.encode({object: {name: Name...}})` type inference) | Not ported; dynamic encode always takes a type name + ABI | json values carry no C++ type information |
| Fixed-size array (`int32[4]`) decode reads a varuint length prefix (upstream bug, encode writes none) | Decode honors the fixed size symmetrically | Upstream decode of a fixed array cannot round-trip its own encode |
| `strictExtensions` decode arg | `Serializer::DecodeOptions{.strictExtensions}` overloads on static and dynamic decode; typed BinaryExtension fields synthesize `abi_traits<T>::abiDefault()`, the dynamic path synthesizes from the ABI (first variant alternative, recursive struct defaults, circular detection) | The custom-class coupling cases (customTypes, abiDefault overrides) are TS runtime machinery; dynamic results keep null members where typed TS structs omit absent optionals |
| Self-referential optional struct fields (`self?: Complex`) | `std::shared_ptr<T>` field maps to `T?` | `std::optional` requires a complete type |
| miniz (BLUEPRINT.md section 3) | Vendored zlib 1.3.1 | pako is byte-identical to zlib, keeping packed-transaction fixture hashes and ESR URIs byte-compatible; miniz streams differ |
| api `FetchProvider` class | `FetchAPIProvider` (wraps the transport `FetchProvider` interface) | Two types cannot share the name in one namespace; the transport interface keeps the blueprint-pinned name |
| `BoolType.from` passes any value through | `bool` fromJSON accepts booleans and 0/1 numbers, rejects the rest | nodeos emits 0/1 for some bool fields |
| Untyped API interfaces (`PushTransactionResponse`, `SendTransactionResponse`, `GetAbiResponse`, params bags) | Plain `json` | Upstream never runs them through the serializer either |
| `ChainDefinition.accountDataType` (a class reference) | Dropped; chain-specific account types are template parameters (`BasicAccountObject<TelosAccountVoterInfo>`) at call sites | C++ cannot store a type in a struct |
| Telos/WAX account objects re-declare `voter_info` on a subclass | `BasicAccountObject<VoterInfo>` template | Field re-declaration has no C++ equivalent |
| `ABICache.pending` promise map | A mutex; concurrent getAbi calls for one account serialize | BLUEPRINT.md 6.3; no promises to share |
| mock-data `makeMockFetch` records `{method, body}` without empty headers | `MockFetchProvider` omits the headers key when empty | Matches the recorded fixture hashes |
| `SigningRequestEncodingOptions.zlib` provider object (compression only when passed) | `bool zlib = true` (built-in zlib); a request decoded/created keeps its flag for later `encode()` | zlib is vendored anyway; pass `{.zlib = false}` for parity with TS call sites that omit the provider |
| `SigningRequest.create` / `identity` / `fromPayload` are async (`createSync` variants exist) | One blocking method each | BLUEPRINT.md 2: no promises |
| Placeholder resolution rebuilds typed action data via `Struct.from` walking decoded objects with `Name.isInstance` checks | JSON walk over the decoded action data replacing exact strings `............1` / `............2` | The decoded representation is json; bare strings equal to a placeholder name only occur as name values |
| `SigningRequest.data` is a `RequestData` struct instance whose `req` variant differs by version | `std::variant<RequestDataV2, RequestDataV3>` member | The two ABI layouts (IdentityV2/IdentityV3) need distinct C++ types |
| Checksum classes are final in dwarfkit up to Phase 1 | `final` removed from `Checksum160/256/512` | `ChainId extends Checksum256` upstream |
| `SigningRequest.getInfoKey(key)` returns string, `(key, type)` overloads infer typed decode | `getInfoKey(key)` string, `getInfoKey<T>(key)` typed, `getInfoKey(key, type)` dynamic json | C++ overload set replaces TS union-typed params |
| protocol-esr imports LoginContext from the session kit | `createIdentityRequest`/`verifyLoginCallbackResponse` take `EsrLoginContext` (appName, chain, chains, esrOptions), the slice they read; the session kit converts | C++ layering runs the other way; protocol_esr builds before session |
| sealed-messages `sealMessage` returns raw Bytes while protocol-esr `sealMessage` returns a SealedMessage | One `sealMessage` (SealedMessage, the plugin-facing one); the raw path is `encryptMessage` | Same name cannot return two types in one namespace |
| `@greymass/miniaes` AES_CBC | trezor Gladman AES (aes/ subset), PKCS7 in dwarfkit | Byte parity proven against @wharfkit/sealed-messages under node (ciphertext, checksum, encoded struct) |
| buoy `Listener` EventEmitter (connect/disconnect/error/message events, 10 min reconnect interval, node ping terminate) | Blocking `receiveMessage(timeout, CancelToken)` loop: heartbeat frames acked inline, socket errors reconnect with the upstream backoff curve, encodings collapse to bytes/text | No event loop (BLUEPRINT.md 5.2); receive() covers the ported call sites |
| `uuid()` from Math.random | Same v4 layout fed from the OS CSPRNG | Only the format is observable |
| `generateReturnUrl` browser sniffing (iOS/Android/browser deep links) | Returns nullopt; createIdentityRequest never sets same_device/return_path | No window/navigator on native; engine adapters own deep links |
| `getUserAgent()` appends navigator.userAgent | "@wharfkit/protocol-esr 1.6.1 dwarfkit/<version>" | No navigator; keeps the upstream prefix wallets may match on |
| `waitForCallback(callbackArgs, buoyWs, t)` translation callback | Fixed upstream default message "The request was cancelled from Anchor." | Session kit translations arrive in Phase 3 |
| `register(context)` plugin method | `register_(context)` | `register` is reserved in C++ |
| Session/SessionKit constructors throw (missing permission, missing fetch) | Type-level requirements: SessionArgs carries a typed PermissionLevel; a missing FetchProvider surfaces as a transport error at call time | No exceptions in the public API |
| `appName` in SessionOptions | In SessionArgs (BLUEPRINT.md 6.6) | Blueprint signature |
| `BrowserLocalStorage` default kit storage | `MemorySessionStorage` default; `FileSessionStorage` for persistence | No browser (BLUEPRINT.md 2) |
| `AbstractUserInterface.translate`/`addTranslations` throw "must be implemented" | translate returns the default value or the key; addTranslations is a no-op | No exceptions; the fallback is useful for UIs without localization |
| `TransactRevision.code` is `String(hook)` (function source) | Hook labels ("original", "beforeSign#N") | C++ closures cannot be stringified; nothing asserts the strings |
| eosjs transact compat (loose header fields upgrade `{actions}` to a transaction) | Dropped; TransactArgs is typed so stray headers cannot ride along | Impossible by construction; pass `.transaction` instead |
| `TransactArgs.request: SigningRequest \| string` | `std::optional<std::variant<SigningRequest, std::string>>` | Union type |
| WalletPlugin get/set accessors and optional methods (`logout?`, `retrievePublicKey?`) | Virtual methods plus `hasLogout()`/`hasRetrievePublicKey()` flags | C++ cannot detect optional overrides |
| `@wharfkit/mock-data` npm package | tests/util mock headers, not shipped | Test-only surface |
| Upstream WalletPluginPrivateKey description templates "undefined" (reads the never-set data.publicKey) | Uses the actual public key string | Faithful reproduction would embed the word "undefined" |
| `processReturnValues` truthiness check on return_value_hex_data | Explicit absent-or-empty-string check | JS falsy semantics |
| BN + js-big-decimal arithmetic in resources | 128-bit integer multiply/divide plus exact decimal division helpers (scale, round half up, parse back) | Same digits on the recorded fixtures; ties round half up where js-big-decimal may differ |
| `Resources.v1` property | `v1()` accessor returning the API view | C++ members cannot safely back-reference a movable parent |
| `PowerUpStateResource.reserved` typed Int64 but asserted against floats | Truncating int64 division, asserted as 0 | The upstream assertion is vacuous (BN truncates both sides to 0) |
| resource provider / autocorrect Transfer and BuyRAMBytes exports | Nested types on the plugin classes | Avoids colliding with the token kit's Transfer |
| Plugin translation bundles (en/ko/zh-Hans/zh-Hant) | Transact/account-creation plugins embed en only (add locales via ui.addTranslations); the anchor and cloudwallet wallet plugins embed all four upstream locales | Escaped-ASCII embedding arrived with the wallet plugins |
| Resource provider 120s fee-prompt expiry timer | Not replicated (prompt has no timeout) | Upstream marks it TODO-remove; needs a timer thread |
| finality plugins schedule polling with setTimeout (checker's hook returns a never-resolving promise) | Blocking waits inside the afterBroadcast hook with configurable delays; the checker returns after its final prompt | No event loop; transact runs on a worker (BLUEPRINT.md 5.2) |
| autocorrect races a cancelable "Checking transaction" prompt against the correction | ui.status message before correcting | Blocking prompts cannot race |
| autocorrect `register` throws without a UI | Installs a hook that reports the error at transact time | No exceptions in the public API |
| Old-antelope test recordings (POST get_info, uncompressed send_transaction, mismatched status text) | Fixtures re-keyed/aligned to the current wire format where signatures proved identical | mock-data lookups are keyed by the exact request bytes; the crypto parity is unchanged |
| `@wharfkit/msigs` loose response interfaces | json responses | Upstream never runs them through the serializer |
| Anchor account creation popup (window.open + postMessage + 500ms close poll) | `openDialog` handler: the embedder opens the url and returns the service payload | No browser window on native; same protocol |
| jungle4 plugin copy-to-clipboard button carries an onClick closure | The prompt button element carries the key as data for the UI to copy | Prompt elements are data, not code |

## contract

- Table rows are `dk::json` values rather than typed row structs. The TS kit decodes rows into `rowType` classes generated per table; dwarfkit decodes with `Serializer::decode(data, type, abi)` into json and leaves typed rows to dkgen output. Typed-row test assertions become field checks.
- `Table::buildParams` emulates the exact JSON key insertion order the TS code produces (spread construction in table.ts plus the client writing `json`/`lower_bound`/`upper_bound` in place) so request bodies hash to the recorded fixture names. Two orders exist: the cursor path `{json, limit, table, code, scope, [index_position], key_type, [lower_bound], [upper_bound], [limit], [reverse]}` and the get path `{table, code, scope, limit, [lower_bound], [upper_bound], [index_position], key_type, json}`.
- `key_type` is always sent, inferred as `i64` for numeric bounds and `name` otherwise, matching what the recorded requests carry even though the TS type allows omitting it.
- `Contract.action` data is encoded at construction (`Action.data` bytes); the TS kit defers with an ABI-carrying Action subclass. Behavior is identical for serialization and transact flows.

## token

- The generated `contracts/system.token.ts` module (typed Types/ActionParams/Contract subclass) becomes `system_token::{abiBlob, abi(), contract()}`: the embedded ABI blob verbatim plus a preconfigured base `Contract`. Typed action/table wrappers are dkgen output.
- The recorded balance fixtures for symbol-code queries carry empty `lower_bound`/`upper_bound` with `key_type:"name"`, an artifact of the older antelope client they were recorded with. Current upstream sources (contract `wrapIndexValue` passing the UInt64 through, the client inferring `key_type:"i64"` and stringifying the bound) produce the bodies dwarfkit produces, so those fixtures are re-keyed under the current-behavior hashes with the recorded responses kept verbatim. The `NOT` symbol fixture is the recorded empty-rows response re-keyed under the bounded query body (a correct bound on an unheld symbol matches no row).
- Upstream's "symbol does not exist" test asserts nothing (a string second argument to `assert.rejects` is an assertion message, not a matcher) and its symbol-mismatch branch is only reachable through the empty-bound quirk. The branch is ported 1:1; the test asserts the missing-row error path that correct bounds produce.
- `balance()` error wrapping drops the JS `Error: ` stringification prefix: `Failed to fetch balance for X: <message>`.

## account

- `AccountKit<DataType>`/`Account<DataType>` mirror the upstream generic as class templates defaulting to `api::v1::AccountObject`. Upstream reads the data type from `chain.accountDataType` at runtime; dwarfkit's ChainDefinition carries no type reference, so the type is chosen at the call site (`AccountKit<WAXAccountObject>`), consistent with the earlier ChainDefinition divergence.
- `ResourceType` is an enum instead of a string union (the unknown-type constructor throw becomes unrepresentable), and `Resource` does not retain the source AccountObject; `toJSON` emits the same fields.
- `Permission` mutators return `Result<void>` in place of throws. The unused exported TS type aliases (PermissionData, ActionData, AddKeyActionParam) are dropped; `LinkedAction` is `api::v1::AccountLinkedAction`.
- The generated `contracts/eosio.ts` module becomes `system_contract::{abiBlob, abi(), contract()}` like the token package's system_token.
- The one recorded balance query with the old-era empty bounds is re-keyed under the current-behavior body (same rationale as the token package).

## wallet-plugin-anchor

- Browser windows become the `openLink` embedder hook (same pattern as the account-creation plugin's openDialog): the native transport uses it for the esr: deep-link auto-launch, the web transport for the authenticator URL. When no hook opens a window the web transport shows the URL as a link prompt (the upstream popup-blocked path). Popup-closed polling is dropped.
- The interactive mode chooser and mid-login transport switch (promptForMode, loginWithSwitch, recoverLogin) rely on prompt button onClick callbacks that the C++ PromptElement cannot carry. Mode selection comes from WalletPluginAnchorOptions.mode, setMode, or per-call arbitrary data {"anchor": {"mode": ...}}, defaulting to the app transport. AnchorMode is an enum; readMode/writeMode/readLoginOptions are otherwise 1:1.
- AnchorRequestCancelledError maps to ErrorKind::Canceled (waitForCallback already returns that kind for rejected payloads). The webFallbackDelayMs timer, isKnownMobile()/isAppleHandheld() user-agent sniffing (a knownMobile option hides the QR) and ledger navigator detection have no browser equivalents.
- The web transport's identityProof is a typed IdentityProof resolved from the callback payload; upstream returns a loose {signature, signedRequest} object there.
- The prompt-vs-callback race collapses to display-prompt-then-block-on-callback: UserInterface::prompt displays and returns, waitForCallback blocks with the transaction expiration as timeout (sign) and the plugin CancelToken.
- signing-request: ResolvedSigningRequest::getIdentityProof on a v2 request now yields an empty scope instead of erroring; upstream's `getIdentityScope()!` flows null into Name.from, which bn.js coerces to 0.

## qrcodegen

- Nayuki's QR Code generator (MIT) is vendored in third_party/qrcodegen for the examples; examples/anchor_login renders prompt qr elements as half-block terminal QR codes.

## wallet-plugin-cloudwallet

- The popup window + window.postMessage exchange becomes the WebViewBridge embedder interface (open/awaitMessage/postMessage/close). The close-listener interval and MessageEvent origin/source validation are the bridge implementation's responsibility; bridge timeouts and closes map to the upstream timeout/closed error strings. The TRANSACTION message carries the serialized transaction as a hex string (a structured-clone Uint8Array has no json form); responses accept hex, arrays, or index-keyed objects.
- validateModifications compares the new action's authorization actor by value. Upstream compares Name objects with ===, which is reference equality and never matches, so its added-action rules never actually reject anything; the port implements the documented intent.
- `localStorage.setItem('connectedType', 'web')` persists into the plugin data. The deprecated mobileAppConnectConfig option is dropped entirely rather than accepted-and-ignored.
- waxSign errors "The Cloud Wallet failed to respond" when the response lacks serializedTransaction, mirroring the upstream isCallback key check. When the transaction was modified, the re-resolved request is built from the returned transaction (same construction as upstream).

## wallet-plugin-cleos

- The copy-to-clipboard button's onClick (navigator.clipboard) has no meaning here; the button element keeps its label for UIs that can render a copy affordance. The prompted command and decoded transaction json (4-space indent, actions replaced in place) match upstream.

## dkgen (@wharfkit/cli generate)

- Emits one self-contained C++ header instead of a TypeScript module: `namespace dwarfkit::gen::<account>` (dots become underscores) with abiBlob/abi(), a Types namespace of DK_STRUCT/DK_VARIANT declarations, and a Contract subclass whose typed action helpers encode the argument struct and hand the hex to the base action(); tables get accessor methods returning the json-row Table. The ActionParams/RowType/TableMap type-level machinery is representable directly as the struct types themselves.
- Type resolution mirrors the cli: one level of abi.types alias resolution, ABI struct/variant names win over builtins, decorators wrap as std::vector / std::optional / BinaryExtension. Struct ordering follows the cli's dependency-first, first-occurrence-wins walk (with a visiting set so self/cyclic references terminate).
- ABI field names that are C++ keywords are a hard error: DK_FIELDS derives the wire name from the member identifier. Method names for actions/tables are sanitized (dots removed, keyword suffix underscore, leading digit prefixed).
- The cli's prettier/eslint pass has no equivalent; output formatting is fixed. action_results interfaces are not emitted (readonly returns json).
- DK_NO_FIELDS was added to the serializer macros for ABIs that declare empty action argument structs (e.g. atomicassets init).

## actionstream

- The event-driven WebSocket client becomes a blocking pull client: heartbeats, catchup, errors and reconnects are processed while next()/nextWithTimeout() pumps the socket, firing the same callbacks. The in-memory action queue, its overflow path (onOverflow/queueSize/overflowCount) and the async iterator do not exist; unread frames stay in the transport and next() is the iterator.
- startSeq: 'head' becomes the startAtHead option (same 2^64-1 sentinel on the wire). Reconnect backoff doubles to reconnectMaxDelay and resets after a connection outlives healthyThreshold, as upstream; the backoff wait happens inside the pump and honors the CancelToken.
- Protocol behavior is 1:1: subscribe/ack message shapes, resume from the last accepted global_seq, the ack watermark (interval, watermark never rewinds), sub_seq gap detection including the omitted-field disable and the restart-at-1-after-reconnect rule, and DataInconsistent for actions without a trx_id.

## atomicassets

- The generated contracts/{atomicassets,atomicmarket,atomictoolsx}.ts modules are dkgen output checked into include/dwarfkit/atomicassets/contracts (namespaces dwarfkit::gen::*), proving dkgen on real generated-module inputs. The Asset object class is AtomicAsset (dwarfkit::Asset is the token amount type).
- Endpoint options and responses are json (the ~1000 lines of typed options interfaces and response Structs are type-level). The wire behavior is exact: serializeQueryParams keeps booleans/numbers and stringifies the rest with JS String semantics (arrays comma-join), fixPostArguments stringifies page/limit/before/after/burned, POST bodies carry Content-Type: application/json, and buildQueryParams form-encodes like URLSearchParams. All 86 recorded fixtures resolve.
- Object getters read the json API data (numeric fields accept the API's string or number forms; null img/name read as empty). Action builders return Result<Action> encoded through the embedded ABIs; ExtendedAsset prices build from the token_precision/token_symbol/amount triple exactly as upstream.

## engine adapters

- ABIDecoder's bounds-check helper is ensureBytes (was ensure): UE defines ensure() as a macro and BLUEPRINT.md bans that name from dwarfkit headers.
- Both adapters ship as source only; there is no UE or godot-cpp toolchain in this repo, so they compile inside an engine project against the prebuilt static library (build instructions in each adapter README). The blueprint interfaces map 1:1; login responses auto-select the first wallet plugin and the chain/permission passed to the async node, since an interactive chooser is the embedder's UI concern.

## packaging (Phase 8)

- find_package(Dwarfkit) uses a hand-rolled config creating imported targets from the installed archives (dwarfkit, dk_trezor_crypto, dk_zlib, secp256k1, secp256k1_precomputed) instead of a CMake export set: the FetchContent-built secp256k1 needs no export support of its own, and the object-library secp256k1_precomputed installs through a wrapper archive. The config forwards cxx_std_20 and MSVC's /Zc:preprocessor (the DK_FIELDS variadic macros need the conformant preprocessor in consumers too).
- The vendored tl::expected and nlohmann headers install alongside dwarfkit's (they appear in public headers). dwarfkit_curl is not installed; engines bring their own transports and other consumers can add_subdirectory/FetchContent the repo.

## antelope p2p

- The EventEmitter surfaces become std::function handlers registered through onData/onError/onClose (provider) and onMessage/onceMessage/onError/onClose (client); handlers fire synchronously when the lower layer feeds data. removeListener takes the id returned at registration (std::function has no equality).
- The heartbeat timer is embedder-supplied through setTimeoutImpl (there is no default event loop); with no impl the heartbeat option is inert. The client owns no socket: P2PProvider implementations bridge to the engine or OS transport, and SimpleEnvelopeP2PProvider ports the 4-byte little-endian framing with the 8 MB read limit unchanged.
- BlockHeader::id() returns Result (hashing the encoded header can surface encode errors); the camelCase TS field names are kept as member names, so json forms match upstream.
