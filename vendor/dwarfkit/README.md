# Dwarfkit

Native C++20 port of [Wharfkit](https://github.com/wharfkit) (the Greymass SDK suite for Antelope blockchains), built as a static library for C++ software, primarily Unreal Engine and Godot.

Same package boundaries, class names, method names, option shapes, hook names and error semantics as Wharfkit; anyone who knows Wharfkit should be able to use Dwarfkit without relearning it. Deviations forced by C++ or game engines (no exceptions, no promises, no browser) are listed in [DIVERGENCES.md](DIVERGENCES.md). Each module is ported file by file against the pinned upstream sources in [PORT_MANIFEST.md](PORT_MANIFEST.md) and proves byte parity with Wharfkit's own recorded fixtures (422 test cases, 2779 assertions).

```cpp
#include <dwarfkit/plugins/wallet/privatekey.hpp>
#include <dwarfkit/session.hpp>
#include <dwarfkit/transport/curl_fetch_provider.hpp>
namespace dk = dwarfkit;

dk::SessionArgs args;
args.chain = dk::Chains::Jungle4();
args.permissionLevel = dk::PermissionLevel::from("myaccount@active").value();
args.walletPlugin = dk::WalletPluginPrivateKey::make("5J...").value();
dk::SessionOptions options;
options.fetch = std::make_shared<dk::CurlFetchProvider>();
dk::Session session(args, options);

auto result = session.transact({.action = dk::json{
    {"account", "eosio.token"},
    {"name", "transfer"},
    {"authorization", dk::json::array({{{"actor", "myaccount"}, {"permission", "active"}}})},
    {"data", {{"from", "myaccount"}, {"to", "teamgreymass"},
              {"quantity", "0.0001 EOS"}, {"memo", "wharf!"}}}}});
if (result) {
    // (*result->response)["transaction_id"]
}
```

## Parity matrix

| Wharfkit package | Dwarfkit module | Status |
| --- | --- | --- |
| @wharfkit/antelope | `dwarfkit/antelope.hpp` (chain types, serializer, crypto, API client) | done, fixture parity |
| @wharfkit/signing-request | `dwarfkit/signing_request.hpp` | done, byte-identical ESR URIs |
| @wharfkit/abicache | `dwarfkit/abicache.hpp` | done |
| @wharfkit/common | `dwarfkit/common.hpp` (Chains, ChainDefinition, Logo) | done |
| @wharfkit/protocol-esr + @greymass/buoy + sealed-messages | `dwarfkit/protocol_esr.hpp` | done, live buoy round-trip verified |
| @wharfkit/session | `dwarfkit/session.hpp` (kit, session, transact pipeline, hooks, storage, UI) | done, broadcast bodies byte-exact |
| @wharfkit/wallet-plugin-privatekey | `dwarfkit/plugins/wallet/privatekey.hpp` | done |
| @wharfkit/wallet-plugin-anchor | `dwarfkit/plugins/wallet/anchor.hpp` | done (openLink hook for browser pieces) |
| @wharfkit/wallet-plugin-cloudwallet | `dwarfkit/plugins/wallet/cloudwallet.hpp` | done (protocol + WebViewBridge) |
| @wharfkit/wallet-plugin-cleos | `dwarfkit/plugins/wallet/cleos.hpp` | done |
| transact plugins (resource-provider, cosigner, explorerlink, finality-checker, autocorrect) | `dwarfkit/plugins/transact/` | done, recorded flows byte-exact |
| account creation (jungle4, anchor) | `dwarfkit/plugins/account_creation/` | done |
| @wharfkit/contract | `dwarfkit/contract.hpp` | done (rows are json) |
| @wharfkit/token | `dwarfkit/token.hpp` | done |
| @wharfkit/account | `dwarfkit/account.hpp` | done (templated account data types) |
| @wharfkit/resources | `dwarfkit/resources.hpp` | done, numeric parity to the unit |
| @wharfkit/msigs | `dwarfkit/msigs.hpp` | done |
| @wharfkit/actionstream | `dwarfkit/actionstream.hpp` | done (blocking pull client) |
| @wharfkit/atomicassets | `dwarfkit/atomicassets.hpp` | done (contracts are dkgen output) |
| @wharfkit/cli `generate` | `dkgen` tool | done, golden-output tests |
| engine adapters | `adapters/unreal`, `adapters/godot` | source complete, build inside an engine project |

Not ported (browser-only or not applicable): web-renderer, react/vue hooks, browser-extension wallet plugins (Wombat, TokenPocket, Scatter...), p2p module. See PORT_MANIFEST.md for every upstream repo's disposition.

## Building

Requires CMake 3.24+ and a C++20 compiler (MSVC 2022, Clang 15+, GCC 12+).

```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

Options: `DK_WITH_CURL` (default ON) builds the `dwarfkit_curl` transport (fetches libcurl 8.10 when not found); `DK_BUILD_TESTS` / `DK_BUILD_TOOLS` (dkgen) / `DK_BUILD_EXAMPLES` default ON at the top level; `DK_LIVE_TESTS` enables tests that hit live chain endpoints.

### Installing / find_package

```
cmake --install build --prefix <prefix>
```

installs the static libraries, headers (dwarfkit plus the vendored tl::expected and nlohmann headers it exposes) and a package config. Consume with:

```cmake
find_package(Dwarfkit REQUIRED)
target_link_libraries(app PRIVATE Dwarfkit::dwarfkit)
```

The curl transport is not part of the install; engines supply their own transports and other consumers can vendor the repo with `add_subdirectory`/FetchContent to get `dwarfkit_curl`.

## Examples and tools

- `examples/transfer_privatekey` - session + transfer on Jungle 4 with a private key.
- `examples/anchor_login` - Anchor login rendering the QR in the terminal, then a transfer.
- `dkgen generate -u <api url> <account> [-f out.hpp]` (or `--abi file.json|file.b64`) - emits a typed contract header: embedded ABI, `Types::` structs, a `Contract` subclass with typed actions and table accessors.

## Migration notes (TypeScript to C++)

- Every throwing API returns `dwarfkit::Result<T>` (`tl::expected<T, dwarfkit::Error>`). `await x()` becomes `DK_TRY(value, x())` inside functions returning a Result, or `if (auto r = x()) { use *r; }` at the edge. There are no exceptions in the public API.
- Untyped JS objects (action data, table rows, API responses, plugin options bags) are `dwarfkit::json` (nlohmann ordered json, insertion-ordered like JS objects). Typed rows and action params come from `dkgen` output instead of TS generics.
- Constructor-throws became type-level requirements: options structs with designated initializers (`{.client = ...}`) replace TS options objects, and invalid combinations fail at compile time or on `from()`.
- Async flows are blocking calls on your worker thread. `UserInterface::prompt` displays and returns; long waits (wallet callbacks) block inside the kit call and honor `CancelToken`s. Never call a kit on a game/UI thread - the engine adapters show the pattern.
- Browser pieces became embedder hooks: popup windows are `openLink`/`WebViewBridge`, `localStorage` is `SessionStorage`, `fetch`/`WebSocket` are `FetchProvider`/`WebSocketProvider` (curl implementations included, engine implementations in the adapters).
- `register` is a reserved word: TS `plugin.register(context)` is `plugin->register_(context)`.

## Layout

```
include/dwarfkit/   public headers (one umbrella per Wharfkit package)
src/                implementation
tools/dkgen/        contract code generator
adapters/unreal/    UE 5.4+ plugin (source; see its README)
adapters/godot/     Godot 4.3+ GDExtension (source; see its README)
tests/              doctest suites + Wharfkit's recorded fixtures
reference/          pinned upstream clones the port is checked against
```

Governance: [BLUEPRINT.md](BLUEPRINT.md) is the executable contract, [PROGRESS.md](PROGRESS.md) tracks phase status, [PORT_MANIFEST.md](PORT_MANIFEST.md) pins upstream versions, [DIVERGENCES.md](DIVERGENCES.md) logs every intentional deviation.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the porting protocol, build
commands and style rules, and [SECURITY.md](SECURITY.md) for reporting
vulnerabilities privately.

## License

[MIT](LICENSE). Dwarfkit is a port of [Wharfkit](https://github.com/wharfkit)
by Greymass, whose BSD-style notice is retained in [NOTICE.md](NOTICE.md)
along with the licenses of vendored dependencies. Dwarfkit is not affiliated
with or endorsed by Greymass.
