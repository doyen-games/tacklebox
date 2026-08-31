# Dwarfkit Blueprint

Native C++ port of Wharfkit (the Greymass SDK suite for Antelope chains) built as a library for C++ software, primarily Unreal Engine and Godot.

This document is the plan a Claude Code session executes. It fixes the architecture, the dependency choices, the fidelity rules, and the phase order. Anything not decided here is decided by reading the Wharfkit source in `reference/` and porting it.

---

## 1. Goal and scope

- A 1:1 port of the full Wharfkit suite: `antelope` core, `common`, `abicache`, `signing-request`, `protocol-esr`, the Session, Contract, Account, Token and Resources kits, every wallet, transact, login and account-creation plugin that can work outside a browser, and a native equivalent of the `@wharfkit/cli` code generator.
- Same package boundaries, class names, method names, option shapes, hook names and error semantics as Wharfkit. Anyone who knows Wharfkit should be able to use Dwarfkit without relearning it.
- Deviate only where C++ or game engines make the JavaScript shape wrong (Promises, exceptions, decorators, browser-only wallets). Every deviation is logged in `DIVERGENCES.md` with a one-line reason.
- Deliverables: one static library `dwarfkit`, an optional `dwarfkit_curl` transport, the `dkgen` tool, thin Unreal and Godot adapters, and a test suite that proves byte parity with Wharfkit using Wharfkit's own fixtures.

Non-goals: the `web-renderer` UI (engines render their own UI against the `UserInterface` contract), browser-extension wallet protocols (Scatter, Wombat, TokenPocket, MetaMask) which need `window.*` injection, and any Node-style event loop.

---

## 2. Fidelity rules

Parity (the default):

1. Module per Wharfkit package. `@wharfkit/session` becomes `dwarfkit/session`, `#include <dwarfkit/session.hpp>`.
2. Same class, method and field names, camelCase preserved (`Name::from`, `toString()`, `equals()`, `session.transact()`, `client.v1.chain.get_info()`). Public TS fields stay public C++ members.
3. Same static factories: every TS `X.from(...)` becomes a C++ `X::from(...)` overload set.
4. Same option objects: TS object literals become C++ structs used with designated initializers (`{.action = a, .broadcast = false}`).
5. Same plugin contracts, hook names, call order and `ui` callbacks.
6. Same JSON output (`toJSON` rules per type) and same binary output (serializer). Proven by fixture tests.
7. Same error messages wherever a Wharfkit test asserts on one.
8. Same source layout: `src/<module>/` mirrors the TS `src/` tree file for file, so a reader can diff the port against the original.

Discretionary deviations (fixed here, logged once in `DIVERGENCES.md`):

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

---

## 3. Toolchain and dependencies

- Language: C++20 subset. Designated initializers, concepts, `std::span`, `<=>`. No modules, no coroutines, no RTTI reliance (`dynamic_cast`, `typeid`, `std::any` are banned) so the library links into Unreal (RTTI off) and godot-cpp (exceptions off) unchanged.
- Compilers: MSVC 2022, Clang 15+, GCC 12+. Windows, Linux, macOS first; Android and iOS are engine-adapter concerns later.
- Build: CMake 3.24+. Options `DK_WITH_CURL` (default ON, OFF for engine builds), `DK_BUILD_TESTS`, `DK_BUILD_TOOLS`, `DK_BUILD_EXAMPLES`. Installs a `DwarfkitConfig.cmake`.
- Exceptions: the core is compiled with exceptions enabled internally so third-party code (nlohmann) is well defined, but nothing escapes; every boundary converts to `Result`. Engine builds never see a throw.

Third-party (vendored under `third_party/`, all permissive licenses, no OpenSSL):

| Need | Choice | Notes |
|---|---|---|
| Errors | `tl::expected` (single header) | `dk::Result<T> = tl::expected<T, dk::Error>` |
| JSON | nlohmann/json (single header) | `dk::json = nlohmann::ordered_json`; parse via a non-throwing helper |
| K1 curve | libsecp256k1 (recovery + ecdh modules, via FetchContent) | Custom nonce function reproduces elliptic's HMAC-DRBG (`pers = [attempt]`) so K1 signatures match Wharfkit byte for byte |
| R1 curve, hashes | trezor-crypto subset (`nist256p1`, `ecdsa`, `bignum`, `sha2`, `ripemd160`, `hmac`) | P-256 sign/verify/recover for R1 and WebAuthn verification; sha256/sha512/ripemd160/hmac for everything |
| CSPRNG | OS shim (`BCryptGenRandom`, `getrandom`, `SecRandomCopyBytes`) | Key generation only |
| Deflate | miniz | ESR uses raw deflate, base64u |
| HTTP + WebSocket | libcurl (optional, system or FetchContent) | `CurlFetchProvider`, `CurlWebSocketProvider` (curl 7.86+) |
| QR | nayuki qrcodegen (optional util) | Anchor login prompts need a QR; adapters render the matrix |
| Tests | doctest (single header) | Fixture replay through `MockFetchProvider` |

Big integers: `Int128`/`UInt128` are two `uint64_t` (no `__int128`, MSVC). Add, subtract, compare, to/from decimal string, ABI encode/decode. Multiply/divide only if a ported call site needs it.

---

## 4. Repository layout

```
dwarfkit/
  CMakeLists.txt
  cmake/                       DwarfkitConfig.cmake.in, warnings, sanitizers
  include/dwarfkit/            public headers, one directory per module
    dwarfkit.hpp               umbrella
    antelope.hpp  common.hpp  abicache.hpp  signing_request.hpp  protocol_esr.hpp
    session.hpp  contract.hpp  account.hpp  resources.hpp  token.hpp
    core/                      result.hpp json.hpp cancel.hpp bytes.hpp
    antelope/  chain/ serializer/ api/ crypto/
    transport/                 fetch_provider.hpp websocket_provider.hpp curl_fetch_provider.hpp ...
    plugins/wallet/ transact/ login/ account_creation/
  src/                         mirrors include/ and the TS src/ trees file for file
  third_party/                 vendored deps (see section 3)
  tools/dkgen/                 code generator (port of @wharfkit/cli generate)
  tests/
    unit/<module>/             ported test files, one per TS test file
    fixtures/<module>/         copied from each repo's test/data
  adapters/unreal/Dwarfkit/    UE plugin (ThirdParty lib module + runtime module)
  adapters/godot/              GDExtension (godot-cpp)
  examples/                    console samples: get_info, transfer with private key, anchor login
  reference/                   gitignored shallow clones of every wharfkit repo
  BLUEPRINT.md  PROGRESS.md  DIVERGENCES.md  PORT_MANIFEST.md  CLAUDE.md
```

Namespaces: everything under `dwarfkit`; recommend `namespace dk = dwarfkit;`. Macros prefixed `DK_`. Sub-namespaces only where Wharfkit has one (`API::v1::chain` types become `dwarfkit::api::v1`).

Targets: one static library `dwarfkit` (all modules), `dwarfkit_curl` (optional transport), `dkgen`, `dwarfkit_tests`.

---

## 5. Cross-cutting design

### 5.1 Errors

```cpp
enum class ErrorKind { Invalid, Api, Transport, Canceled, Plugin, Storage, Unsupported, NotFound, Internal };
struct Error { ErrorKind kind; std::string message; int code = 0; json details; };   // Api: code = HTTP status, details = chain error object
template <class T> using Result = tl::expected<T, Error>;
#define DK_TRY(var, expr) auto&& DK_CAT(_r, __LINE__) = (expr); if (!DK_CAT(_r, __LINE__)) return tl::unexpected(std::move(DK_CAT(_r, __LINE__).error())); auto var = std::move(*DK_CAT(_r, __LINE__));
```

`APIError` from Wharfkit maps to `ErrorKind::Api` with the same `message`, `code` and `details` shape, and the same helpers (`hasChainError`, `chainError`) as free functions.

### 5.2 Concurrency

- The library is synchronous and thread-agnostic. Every `async` function in Wharfkit becomes a blocking function returning `Result<T>`.
- `dk::async(f)` is a one-liner returning `std::future` for callers that want it.
- `CancelToken` (shared atomic flag + condition variable) is the one concurrency primitive. UI prompts and callback waits take one so a wallet flow can race "user closed the prompt" against "wallet answered".
- Rule for adapters and examples: never call a kit on the game thread; run on a worker and deliver results with the engine's delegate or signal mechanism.

### 5.3 Transport and storage interfaces

```cpp
struct FetchRequest  { std::string url, method{"POST"}, body; std::vector<std::pair<std::string,std::string>> headers; };
struct FetchResponse { int status; std::string body; std::vector<std::pair<std::string,std::string>> headers; };
struct FetchProvider { virtual Result<FetchResponse> fetch(const FetchRequest&) = 0; virtual ~FetchProvider() = default; };
struct WebSocketProvider { virtual Result<void> connect(std::string_view url) = 0; virtual Result<Bytes> receive(std::chrono::milliseconds timeout, CancelToken) = 0; virtual Result<void> send(std::span<const uint8_t>) = 0; virtual void close() = 0; };
struct SessionStorage { virtual Result<void> write(std::string_view key, std::string_view data) = 0; virtual Result<std::optional<std::string>> read(std::string_view key) = 0; virtual Result<void> remove(std::string_view key) = 0; };
```

Same key scheme as Wharfkit (`wharf-<appName>-...`) so a stored session is recognisable.

### 5.4 Dynamic values

`dk::json` plays the role of plain JS objects everywhere Wharfkit accepts or returns one (action data, table rows, plugin `data`, ESR info pairs). Every chain type has `nlohmann::adl_serializer` specialisations, so `json j = transfer;` and `auto t = j.get<Transfer>();` work, and `Serializer::objectify(x)` is `json(x)`.

### 5.5 Struct reflection (replaces decorators)

```cpp
struct Transfer : dk::Struct<Transfer> {
  DK_STRUCT("transfer")
  Name from; Name to; Asset quantity; std::string memo;
  DK_FIELDS(from, to, quantity, memo)
};
```

- `DK_STRUCT(abi_name)` defines `using Self`, `abiName`.
- `DK_FIELDS(...)` expands to a `constexpr` tuple of `(name, member pointer)` used by the encoder, decoder, JSON serialiser, `equals`, and `Serializer::synthesize`. Up to 32 fields.
- Field ABI type names derive from the member C++ type through `dk::abi_type<T>::name` (`Name` becomes `name`, `std::vector<T>` becomes `T[]`, `std::optional<T>` becomes `T?`, `dk::BinaryExtension<T>` becomes `T$`). `DK_STRUCT_BASE(abi_name, Base)` for inheritance, `DK_VARIANT(TypeName, abi_name, Ts...)` for variants, `DK_TYPE_ALIAS(TypeName, abi_name, Underlying)` for aliases.
- Explicit per-field ABI names (for typedef names in synthesized ABIs) are deferred until a ported call site needs them.

### 5.6 Type map (ABI to C++)

| ABI | C++ | ABI | C++ |
|---|---|---|---|
| bool | bool | name | `Name` (`_n` literal) |
| int8..int64, uint8..uint64 | `int8_t`..`uint64_t` | bytes | `Bytes` |
| int128, uint128 | `Int128`, `UInt128` | string | `std::string` |
| varint32, varuint32 | `VarInt32`, `VarUInt32` | checksum160/256/512 | `Checksum160/256/512` |
| float32, float64 | `float`, `double` | public_key, signature | `PublicKey`, `Signature` |
| float128 | `Float128` (16 raw bytes, hex only) | symbol, symbol_code | `Symbol`, `SymbolCode` |
| time_point, time_point_sec | `TimePoint`, `TimePointSec` | asset, extended_asset | `Asset`, `ExtendedAsset` |
| block_timestamp_type | `BlockTimestamp` | T[], T?, T$ | `std::vector<T>`, `std::optional<T>`, `BinaryExtension<T>` |
| variant | `Variant<Ts...>` | struct | `DK_STRUCT` type or `json` when dynamic |

Wharfkit's `Int` wrapper classes (`UInt64.from`, `.adding()`, `.toNumber()`) map to native integers plus free helpers only where a ported API returns them. `toJSON` number-versus-string rules are ported from `integer.ts` exactly.

### 5.7 Literals and one-liners

`"eosio.token"_n` (constexpr `Name`), `"1.0000 WAX"_asset`, `"4,WAX"_symbol`, `"active"_n`. `PermissionLevel::from("t.break@active")`. Every `from()` also accepts `json`.

---

## 6. Module map

Each module lists the Wharfkit source to port and the C++ surface. Port file by file from `reference/<repo>/src/`; the TS file list below is the checklist.

### 6.1 `dwarfkit/antelope` (from `wharfkit/antelope`)

TS files: `chain/{abi,action,asset,authority,block-id,blob,bytes,checksum,float,integer,key-type,name,permission-level,private-key,public-key,signature,struct,time,transaction,type-alias,variant}.ts`, `serializer/{builtins,decoder,encoder,serializer}.ts`, `api/{client,provider}.ts`, `api/v1/{chain,history,types}.ts`, `crypto/{base58,curves,generate,get-public,get-shared,recover,sign,verify}.ts`, `utils.ts`.

Surface:

- Types listed in 5.6 with the full Wharfkit method set: `from`, `toString`, `toJSON`, `equals`, `hexString`, `arrayBuffer`, `rawValue`, `Name.pattern`, `Asset.units/value/symbol`, `Symbol.precision/code/name`, `Checksum256::hash`, `TimePoint::fromMilliseconds/toDate`, `BlockTimestamp`.
- Keys: `PublicKey::from` (legacy `EOS...` and `PUB_K1_/PUB_R1_/PUB_WA_`), `toLegacyString(prefix)`, `PrivateKey::from` (WIF and `PVT_K1_`), `generate(KeyType)`, `toPublic`, `signDigest`, `signMessage`, `sharedSecret`, `Signature::from`, `verifyDigest`, `verifyMessage`, `recoverDigest`, `recoverMessage`. WA keys parse, serialise and verify; signing is `Unsupported`.
- Base58 with all four check variants (`decodeCheck`, `decodeRipemd160Check`, `encodeCheck`, `encodeRipemd160Check`), base64, hex.
- `ABI` (`from` json/bytes/`ABIDef`, `resolveType`, `resolveAll`, `getStruct`, `getVariant`, `getAction`, `getTable`, `getActionResult`), `ABIEncoder`, `ABIDecoder`, `Serializer` with static and dynamic overloads:

```cpp
template <class T> Result<Bytes> Serializer::encode(const T&);
Result<Bytes> Serializer::encode(const json&, std::string_view type, const ABI&);
template <class T> Result<T> Serializer::decode(std::span<const uint8_t>);
Result<json> Serializer::decode(std::span<const uint8_t>, std::string_view type, const ABI&);
template <class T> std::string Serializer::stringify(const T&);
template <class T> ABI Serializer::synthesize();
```

- `Action` (`from` with typed data, `json` + ABI, or raw `Bytes`; `decodeData<T>()`, `decodeData(abi)`), `Transaction` (`signingDigest(chainId)`, `signingData`, `id`, `expiration` helpers), `SignedTransaction`, `PackedTransaction` (`fromSigned(signed, compression)`, `getTransaction`, `getSignedTransaction`), `TransactionHeader`, `TransactionReceipt`, `Authority`, `PermissionLevel`, `KeyWeight`, `PermissionLevelWeight`, `WaitWeight`, `BlockId`, `Blob`.
- `APIClient({.url, .provider?, .fetch?, .headers?})`, `APIProvider`, `FetchProvider` (default `CurlFetchProvider` when `DK_WITH_CURL`), `client.call<T>({.path, .params})`, `client.v1.chain.*` and `client.v1.history.*` with the exact Wharfkit method list (`get_abi`, `get_account`, `get_accounts_by_authorizers`, `get_activated_protocol_features`, `get_block`, `get_block_header_state`, `get_block_info`, `get_code`, `get_currency_balance`, `get_currency_stats`, `get_info`, `get_producer_schedule`, `get_producers`, `get_raw_abi`, `get_scheduled_transactions`, `get_table_by_scope`, `get_table_rows` typed and untyped, `get_transaction_status`, `compute_transaction`, `push_transaction`, `send_transaction`, `send_transaction2`, `send_read_only_transaction`; history `get_actions`, `get_transaction`, `get_key_accounts`, `get_controlled_accounts`). All response structs from `api/v1/types.ts` become `DK_STRUCT`s under `dwarfkit::api::v1`.

### 6.2 `dwarfkit/common` (from `wharfkit/common`)

`Chains` constants (port the current list from source; check for the Vaulta rename and any additions), `ChainDefinition` (`from`, `getClient`, `getLogo`, `explorer`, `coreToken`, `systemContract`), `chainNames`, `chainLogos`, `chainIdsToIndices`, `ExplorerDefinition::url(id)`, `Logo`, `Canceled` (becomes `ErrorKind::Canceled`), `Fetch` (becomes `FetchProvider`).

### 6.3 `dwarfkit/abicache` (from `wharfkit/abicache`)

`ABICache(client)`: `getAbi(account)`, `setAbi`, `cache`, `pending` (a mutex replaces the promise map).

### 6.4 `dwarfkit/signing_request` (from `wharfkit/signing-request`)

`SigningRequest::create({.action | .actions | .transaction | .identity, .chainId, .callback, .broadcast, .info}, {.abiProvider, .zlib, .scheme})`, `SigningRequest::from(uri, opts)`, `identity`, `encode(compress, slashes, scheme)`, `resolve(abis, signer, ctx)`, `resolveActions`, `resolveTransaction`, `getRawActions`, `getChainId`, `getChainIds`, `isMultiChain`, `isIdentity`, `getIdentity`, `getIdentityKey`, `getIdentityScope`, `getInfo`, `getInfoKey`, `setInfoKey`, `getRequiredAbis`, `fetchAbis`, `getMetadata`, `setSignature`, `getCallback`, `shouldBroadcast`, `hasCallback`, `ResolvedSigningRequest` (`transaction`, `signer`, `request`, `chainId`, `getCallback(signatures, blockNum)`, `serializedTransaction`, `signingDigest`), `ResolvedAction`, `ResolvedCallback`, `CallbackPayload`, `TransactionContext`, `PlaceholderName`, `PlaceholderPermission`, `PlaceholderAuth`, `ChainAlias`/`ChainId` variants, `RequestDataV2/V3`, `IdentityV2/V3`, `RequestFlags`, base64u. Deflate through miniz; compression bytes may differ from pako so parity tests compare decoded payloads, not compressed URIs.

### 6.5 `dwarfkit/protocol_esr` (from `wharfkit/protocol-esr`, `greymass/buoy-client`)

`sealMessage`, `unsealMessage`, `SealedMessage`, `LinkInfo`, `LinkCreate`, `createIdentityRequest`, `waitForCallback(url, ws, timeout, token)`, `verifyLoginCallbackResponse`, `extractSignaturesFromCallback`, `isCallback`, `setTransactionCallback`, `generateReturnUrl`, `getUserAgent`, `prepareCallbackChannel`, `BuoyOptions`, buoy `send` (HTTP POST) and `receive` (WebSocket) over the transport interfaces, uuid v4.

### 6.6 `dwarfkit/session` (from `wharfkit/session`)

TS files: `kit.ts`, `session.ts`, `login.ts`, `transact.ts`, `plugins.ts`, `storage.ts`, `ui.ts`, `utils.ts`, `translations`, `types.ts`.

```cpp
SessionKit(SessionKitArgs{.appName, .chains, .ui, .walletPlugins}, SessionKitOptions{.abiCache, .allowModify, .expireSeconds, .fetch, .loginPlugins, .storage, .transactPlugins, .transactPluginsOptions, .accountCreationPlugins});
Result<LoginResult> login(const LoginOptions& = {});          // {context, response, session}
Result<void> logout(std::shared_ptr<Session> = nullptr);
Result<std::shared_ptr<Session>> restore(const RestoreArgs& = {});
Result<std::vector<std::shared_ptr<Session>>> restoreAll();
Result<std::vector<SerializedSession>> getSessions();
Result<void> persistSession(const Session&, bool setAsDefault = true);
Result<CreateAccountResult> createAccount(const CreateAccountOptions&);   // verify against current session source
Session(SessionArgs{.chain, .permissionLevel, .walletPlugin, .appName}, SessionOptions{.abiCache, .allowModify, .broadcast, .expireSeconds, .fetch, .storage, .transactPlugins, .transactPluginsOptions, .ui});
Result<TransactResult> transact(const TransactArgs&, const TransactOptions& = {});   // args: action | actions | transaction | request
Result<std::vector<Signature>> signTransaction(const Transaction&);
Result<SigningRequest> createRequest(const TransactArgs&, const ABICache&);
SerializedSession serialize() const;
```

Transact pipeline, in Wharfkit order: build `TransactContext`; create `SigningRequest`; `ui.onTransact`; `beforeSign` hooks (track `revisions`, reject modification when `allowModify` is false); resolve (ABIs via `ABICache`, TAPoS via `get_info` and `get_block`); `ui.onSign`; `walletPlugin.sign`; `ui.onSignComplete`; `afterSign`; if broadcast: `ui.onBroadcast`, `beforeBroadcast`, `send_transaction`, `afterBroadcast`, `ui.onBroadcastComplete`; `ui.onTransactComplete`; decode `returns` from `action_results`; `ui.onError` on any failure.

Login pipeline: `LoginContext` (chains, wallet plugins, `uiRequirements`); `ui.onLogin`; `ui.login(context)` yields `{walletPluginIndex, chainId?, permissionLevel?}`; `beforeLogin`; `walletPlugin.login`; build `Session`; `afterLogin`; `persistSession`; `ui.onLoginComplete`.

Plugin contracts, unchanged names: `AbstractTransactPlugin {id, translations, register(TransactContext&)}` with hooks `beforeSign`, `afterSign`, `beforeBroadcast`, `afterBroadcast`; `AbstractLoginPlugin` with `beforeLogin`, `afterLogin`; `AbstractWalletPlugin {id, config{requiresChainSelect, requiresPermissionSelect, supportedChains}, metadata{name, description, logo, homepage, download, publicKey}, data (json), login(LoginContext&), sign(ResolvedSigningRequest&, TransactContext&), serialize()}`; `AbstractAccountCreationPlugin`.

`UserInterface` (pure virtual, same method set): `login`, `onError`, `onLogin`, `onLoginComplete`, `onTransact`, `onTransactComplete`, `onSign`, `onSignComplete`, `onBroadcast`, `onBroadcastComplete`, `onAccountCreate`, `onAccountCreateComplete`, `prompt(PromptArgs, CancelToken)`, `status`, `translate`, `addTranslations`, `getTranslate`. `PromptArgs{title, body, elements}` with element kinds `accept, asset, button, close, countdown, link, qr, textarea`. Ships `ConsoleUserInterface` (stdin/stdout, for examples and tests) and `NullUserInterface`.

Translations: key to string map with namespaces, ported from `translations/`.

### 6.7 `dwarfkit/contract` (from `wharfkit/contract`)

`ContractKit({.client}, {.abiCache})`, `kit.load(name)`, `Contract({.abi, .account, .client})`, `contract.action(name, data, {.authorization})`, `actions`, `actionNames`, `tables`, `tableNames`, `table(name, scope)`, `readonly(name, data)` (read-only transaction, returns decoded `action_results`), `ricardian(name)`, `Table` (`get`, `query`, `all`, `first`, `scopes`, `cursor`), `TableCursor` (`next`, `all`, `reset`, `endReached`, `limit`, `rows_per_api_request`, iterator support), `QueryParams`, `Query`, key type conversions for `lower_bound`/`upper_bound`/`index_position`. Rows return `json`; `table.all<T>()` converts.

### 6.8 `dwarfkit/account` (from `wharfkit/account`)

`AccountKit(chain, {.client, .contract})`, `kit.load(name)`, `Account` (`data`, `permission(name)`, `permissions`, `resource(type)`, `resources()`, `balance(symbol, contract)`, `systemContract`, `buyRam`, `buyRamBytes`, `sellRam`, `delegate`, `undelegate`, `transfer`, `setPermission`, `removePermission`, `linkauth`, `unlinkauth`, and whatever else the current source has), `Permission` (`keys`, `accounts`, `waits`, `addKey`, `removeKey`, `addAccount`, `removeAccount`, `addWait`, `removeWait`, `linked_actions`). The bundled `eosio` system contract ABI JSON becomes a string constant.

### 6.9 `dwarfkit/resources` (from `wharfkit/resources`)

`Resources({.api, .sampleAccount, .symbol, .url, .fetch})`, `v1.powerup.get_state()` (`PowerUpState` with `cpu`/`net` `price_per_ms`, `frac`, `weight`), `v1.rex.get_state()` (`REXState`, `price_per`), `v1.ram.get_state()` (`RAMState`, `price_per_kb`, `price_per`), `getSampledUsage()`, `SampleUsage`, `Delegated` if present. Port the numeric code exactly; these are the calculations games rely on for fee estimates.

### 6.10 `dwarfkit/token` (from `wharfkit/token`)

`TokenKit(chain, {.client, .contract, .tokens})`, `kit.load(contract)`, `Token` (`balance`, `transfer`, `stats`, `hasToken`, `precision`), bundled `eosio.token` ABI constant.

### 6.11 Wallet plugins (`dwarfkit/plugins/wallet/`)

| Plugin | Native status | Notes |
|---|---|---|
| `WalletPluginPrivateKey` | Full | First plugin; unlocks the transact tests |
| `WalletPluginMock` | Full | Ported for the session test suite |
| `WalletPluginAnchor` | Full | Identity request, `ui.prompt` with `qr` + `link` elements, buoy wait over `WebSocketProvider`, sealed direct sends to the stored channel, session `data` persistence. Desktop shows QR or deep link; mobile opens `esr://` |
| `WalletPluginCloudWallet` | Protocol layer plus a `WebViewBridge` interface | MyCloudWallet works through a popup and `postMessage`; native needs an embedded web view. Unreal: CEF WebBrowser can bind the bridge. Godot: needs a third-party web view extension. Ship the protocol so an adapter can finish it |
| `WalletPluginCleos` | Full, dev only | Spawns `cleos sign`; useful on TWIG test nets |
| Scatter, Wombat, TokenPocket, MetaMask, `protocol-scatter` | Not ported | Browser injection protocols; recorded in `DIVERGENCES.md` |

### 6.12 Transact, login and account-creation plugins

Port all of: `TransactPluginResourceProvider` (`allowFees`, `endpoints`, `maxFee`, fee prompt, `noop` prepend, response validation), `TransactPluginCosigner`, `TransactPluginExplorerLink`, `TransactPluginFinalityChecker`, `TransactPluginFinalityCallback`, `TransactPluginAutocorrect`, `TransactPluginMock`, the msig proposal plugin (currently rc on npm), `transact-plugin-template` and `login-plugin-template` as `examples/`, `AccountCreationPluginGreymass` (opens a URL, waits for the ESR-style callback) and the Jungle test-account plugin (API call). Names, options and hook behaviour come from each repo's `src/index.ts`.

### 6.13 Other packages

Enumerate the full wharfkit org in Phase 0 (61 repos at last count). Known extras to classify: the Atomic Assets client, the Shamir secret-sharing package, `mock-data`, `starter-*`, `sessionkit` (meta), `web-renderer` (reference only for prompt semantics), `cli`. Port library packages; treat starters and web-only packages as reference.

---

## 7. `dkgen` (from `wharfkit/cli`)

`dkgen generate -u <api url> <account> [-f out.hpp]` fetches the ABI and emits a header mirroring `@wharfkit/cli` output: `namespace <account>` with `DK_STRUCT` types for every ABI struct in dependency order, `DK_VARIANT`/`DK_TYPE_ALIAS` where the ABI has them, a `Contract` subclass with typed `action()` helpers per action, typed `table()` accessors per table, and the ABI JSON as a constant. Also accepts `--abi file.json` for offline use (TWIG contracts). Reuses `dwarfkit/antelope` and `dwarfkit/contract`.

---

## 8. Engine adapters

Both adapters are thin: they implement the four interfaces (`FetchProvider`, `WebSocketProvider`, `SessionStorage`, `UserInterface`), marshal to the game thread, and expose async wrappers. No kit logic lives in an adapter.

### 8.1 Unreal (`adapters/unreal/Dwarfkit/`)

- `Source/ThirdParty/DwarfkitLib/DwarfkitLib.Build.cs`: prebuilt `dwarfkit` static lib + headers, built with `DK_WITH_CURL=OFF` and UE's CRT settings. Include through `THIRD_PARTY_INCLUDES_START/END`.
- `Source/Dwarfkit/`: `FDkUnrealFetchProvider` (FHttpModule request, worker blocks on an `FEvent`), `FDkUnrealWebSocketProvider` (FWebSocketsModule), `FDkUnrealStorage` (`FPaths::ProjectSavedDir()/Dwarfkit/`), `FDkUnrealUserInterface` forwarding to `UDwarfkitUI` (Blueprint-implementable events, dispatched to the game thread and awaited with an `FEvent`), `UDwarfkitSubsystem` (GameInstance subsystem owning the `SessionKit`) and `UBlueprintAsyncActionBase` nodes for `Login`, `Transact`, `Restore`.
- Assumes UE 5.4+. No macro named `check` or `ensure` anywhere in Dwarfkit headers.

### 8.2 Godot (`adapters/godot/`)

- GDExtension on godot-cpp (Godot 4.3+), compiled as C++20 with the same prebuilt static lib.
- `DkSessionKit`, `DkSession`, `DkContractKit`, `DkAPIClient` as `RefCounted` wrappers; one `json <-> Variant` conversion function; signals `login_completed`, `transact_completed`, `error`, `status`; `DkUserInterface` as a class scripts extend with `_login`, `_prompt`, `_status` virtuals, invoked via `call_deferred` and awaited with a `Semaphore`.
- Providers on `HTTPClient` and `WebSocketPeer` polled from the worker thread; storage under `user://dwarfkit/`.

---

## 9. Testing and parity

- Every TS test file gets a doctest file with the same name and cases. Fixtures are copied from each repo's `test/data` into `tests/fixtures/<module>/`.
- `MockFetchProvider` replays fixtures with the same key scheme as `@wharfkit/mock-data` (port its `makeMockFetch`), so recorded API responses drive the session, contract, account, token and resources tests unchanged.
- Parity assertions: identical bytes from the serializer, identical `toJSON` strings, identical K1 signatures for a fixed key and digest, identical `signingDigest`, identical ESR payloads after decode, identical resource price calculations.
- Live tests (real chain, real buoy) are opt-in behind `DK_LIVE_TESTS` and pointed at Jungle 4 and WAX testnet.
- Sanitizers on in CI-style local runs (`-fsanitize=address,undefined` on Clang/GCC).

---

## 10. Phase plan

Each phase ends green (`cmake --build && ctest`), with `PROGRESS.md` ticked, `PORT_MANIFEST.md` updated (repo, commit hash, version, status), and a commit.

### Phase 0: Bootstrap

- [ ] Repo skeleton, CMake, options, warnings-as-errors, `third_party/` vendoring, doctest smoke test.
- [ ] `scripts/fetch_reference.sh`: `gh repo list wharfkit --limit 100 --json name` then shallow clone all into `reference/`, plus `greymass/buoy-client`. Classify every repo in `PORT_MANIFEST.md` (port / reference only / skip, with reason).
- [ ] Copy the phase checklists into `PROGRESS.md`; create `DIVERGENCES.md` seeded from section 2; create `CLAUDE.md` from section 11.

### Phase 1: antelope core

- [ ] `core/`: `Result`, `Error`, `json` helpers, `Bytes`, hex, base64, base58 (all check variants), sha256/sha512/ripemd160/hmac, CSPRNG shim.
- [ ] Chain types: integers (incl. 128-bit, var ints, `toJSON` rules), `Float128`, `Name` + `_n`, `Asset`/`Symbol`/`SymbolCode`/`ExtendedAsset` + literals, time types, checksums, `BlockId`, `Blob`.
- [ ] Crypto: K1 via libsecp256k1 with the elliptic-compatible nonce function; R1 via trezor-crypto; WA parse/verify; `PublicKey`, `PrivateKey`, `Signature` full method sets; key string formats.
- [ ] `ABI` model and `resolveType`, `ABIEncoder`/`ABIDecoder`, builtins, `Serializer` (static, dynamic, `synthesize`, `stringify`), `DK_STRUCT`/`DK_FIELDS`/`DK_VARIANT`/`DK_TYPE_ALIAS`/`BinaryExtension`, `Action`, `Transaction`, `SignedTransaction`, `PackedTransaction`, authority types.
- [ ] `APIClient`, `APIProvider`, `FetchProvider`, `CurlFetchProvider`, `MockFetchProvider`, `v1.chain` and `v1.history` with all `types.ts` structs.
- [ ] All `antelope/test/tests/*.ts` ported and green against copied fixtures.

### Phase 2: common, abicache, signing-request, protocol-esr

- [ ] `common`: `Chains`, `ChainDefinition`, explorers, logos.
- [ ] `abicache`.
- [ ] `signing_request` with miniz and base64u; all its tests.
- [ ] `protocol_esr` + buoy send/receive over the transport interfaces; `CurlWebSocketProvider`; uuid v4; unit tests with a fake `WebSocketProvider`.

### Phase 3: session and plugins

- [ ] `session`: kit, session, login, transact pipeline, plugin base classes, `UserInterface`, storage, translations, `ConsoleUserInterface`, `NullUserInterface`, `FileSessionStorage`, `MemorySessionStorage`.
- [ ] `WalletPluginPrivateKey`, `WalletPluginMock`, `TransactPluginMock`; session test suite green.
- [ ] `TransactPluginResourceProvider`, `Cosigner`, `ExplorerLink`, `FinalityChecker`, `FinalityCallback`, `Autocorrect`, msig plugin; their tests.
- [ ] Login and account-creation plugin bases; Greymass and Jungle account-creation plugins.
- [ ] `examples/transfer_privatekey` runs against Jungle 4.

### Phase 4: contract, account, resources, token

- [ ] `contract` + tests.
- [ ] `resources` + tests (numeric parity).
- [ ] `token` + tests.
- [ ] `account` + tests.
- [ ] Any additional library packages found in Phase 0 (Atomic Assets client, Shamir).

### Phase 5: wallets

- [ ] `WalletPluginAnchor` with qrcodegen util; `examples/anchor_login` (console prints QR as text).
- [ ] `WalletPluginCloudWallet` protocol layer + `WebViewBridge` interface.
- [ ] `WalletPluginCleos`.

### Phase 6: dkgen

- [ ] Generator, offline `--abi` mode, golden-output tests for `eosio.token`, `eosio`, `atomicassets`.

### Phase 7: engine adapters

- [ ] Unreal plugin, sample map with login + transfer.
- [ ] Godot GDExtension, sample scene with login + transfer.

### Phase 8: packaging and docs

- [ ] `find_package(Dwarfkit)` install, versioning, README with the TS-to-C++ parity matrix, migration notes, `DIVERGENCES.md` final pass.

---

## 11. Claude Code session protocol (copy into `CLAUDE.md`)

```
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
```

---

## 12. Open questions to settle during Phase 0

1. Exact repo list and which of the 61 are library code (port), templates (examples), or web-only (reference).
2. Whether `Chains` in `common` reflects the Vaulta rename and any chains added since the last release.
3. MyCloudWallet strategy: embedded web view in Unreal via CEF, or a hosted bridge page that relays `postMessage` to a local HTTP callback.
4. Whether `session` currently exposes `createAccount` and the account-creation plugin interface as described in 6.6.
5. Target engine versions (assumed UE 5.4+, Godot 4.3+) and whether the Godot build should stay on C++17 (would cost designated initializers in the adapter only; the core stays C++20).
