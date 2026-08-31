# Port manifest

One row per upstream repo (all of github.com/wharfkit at 2026-08-26, plus greymass/buoy-client). Classification: **port** (becomes a Dwarfkit module), **reference** (consulted, not ported), **skip** (not applicable). Status: `-` (not started), `wip`, `done vX` (ported at upstream version X). Commits are the shallow-clone HEADs in `reference/`; refresh a row whenever a module is re-synced against upstream.

34 repos classified port, 24 reference, 23 skip.

| Repo | Version | Commit | Classification | Status | Notes |
|---|---|---|---|---|---|
| abicache | 1.2.4 | b8fd381 | port | done 1.2.4 | tests green against copied fixtures |
| account | 1.4.1 | fd370fe | port | done 1.4.1 | AccountKit/Account templated on account data type; Permission/Resource; system_contract embedded ABI |
| account-creation-plugin-anchor | 1.4.0 | d5138c2 | port | done 1.4.0 | popup + postMessage becomes an openDialog embedder hook |
| account-creation-plugin-jungle4 | 1.2.0 | b9f2fab | port | done 1.2.0 | upstream ships only a commented template test; faucet flow covered |
| account-creation-plugin-metamask | 1.3.0 | 3b00177 | skip | - | needs the MetaMask browser extension |
| account-creation-plugin-template | 1.0.0 | 653b653 | reference | - | template; informs examples/ |
| actionstream | 0.4.0 | a7c8608 | port | done 0.4.0 | blocking pull client over WebSocketProvider; queue/overflow path not applicable (DIVERGENCES) |
| antelope | 1.2.0 | bb3c9fb | port | wip (all but p2p/) | Phase 1 core done: chain, serializer, crypto, api all ported with tests green. p2p/ deferred (needs a socket interface decision) |
| antelope-rs | - | 8ff21c7 | reference | - | Rust port; cross-check when the TS is ambiguous |
| api-client-template | 0.0.0 | 367bbc9 | reference | - | template |
| assets | - | 52cc2be | reference | - | brand/media assets, no code |
| atomicassets | 1.3.1 | 6b3a08e | port | done 1.3.1 | endpoints json in/out; contracts are dkgen output; objects/kits typed |
| bundle | 0.1.2 | 2e5e6fb | reference | - | npm meta-bundle, no logic |
| buoy-client | 1.0.4 | 7dc9c71 | port | done 1.0.4 | send/receive/Listener as blocking calls over the transport interfaces; live cb.anchor.link round-trip verified |
| chain-logo | - | 47e69e4 | reference | - | logo CDN assets |
| chains | - | EMPTY | skip | - | empty repository |
| cli (dkgen) | 2.11.0 | 0a2848c | port | - | Phase 6, as dkgen (generate subcommand only) |
| common | 1.5.0 | 31f7106 | port | done 1.5.0 | Chains/ChainDefinition/explorer/logo/token; Canceled maps to ErrorKind::Canceled |
| conformance | 0.1.0 | 39834b9 | reference | - | float-op oracle contract on Jungle 4 (conform.gm); optional DK_LIVE_TESTS consumer for float parity |
| console-renderer | 0.1.1 | b7146d3 | reference | - | informs ConsoleUserInterface |
| contract | 1.3.0 | 2c98897 | port | done 1.3.0 | kit/contract/table/utils; rows as json; request bodies key-order-matched to recordings |
| data-type-template | 0.0.0 | e9c5f35 | reference | - | template |
| discussions | - | d84533e | skip | - | GitHub discussions repo, no code |
| docs | - | 8d9c893 | reference | - | documentation site |
| example-nodejs | 1.0.0 | 4560e97 | reference | - | informs examples/ |
| example-p2pclient | 1.0.0 | 4c24cba | reference | - | pairs with antelope p2p/ decision |
| example-vite-react-ts | 0.0.0 | 6854e08 | reference | - | web example |
| example-vite-svelte-ts | 0.0.0 | e7fb1b5 | reference | - | web example |
| example-vite-sveltekit-ts | 0.0.1 | ee1af02 | reference | - | web example |
| example-vite-vue-ts | 0.0.0 | 4a38f53 | reference | - | web example |
| hyperion | 1.0.5 | be41ae9 | port | - | Phase 4 extras; Hyperion API client |
| issues | - | 4823d3e | skip | - | meta repo, no code |
| js | 4.0.0-rc2 | 639cf3a | reference | - | upcoming all-in-one v4 bundle; watch for API changes |
| login-plugin-template | 1.0.0 | 0610c4b | reference | - | template; informs examples/ |
| mock-data | 1.3.1 | 9cf701d | port | done 1.3.1 | tests/util/mock_fetch_provider.hpp + mock_session.{hpp,cpp}; not shipped in the library |
| msigs | 0.3.1 | c8ccca7 | port | done 0.3.1 | MsigsClient with json responses; representative test subset (api.ts repeats shapes) |
| protocol-esr | 1.6.1 | 1b61ed2 | port | done 1.6.1 | createIdentityRequest takes EsrLoginContext until the session kit lands |
| protocol-scatter | 1.4.0 | 846bd39 | skip | - | browser window injection |
| resources | 1.6.0 | 0af2e3c | port | done 1.6.0 | RAM/REX/PowerUp numeric parity on recorded eos/jungle/wax fixtures |
| roborovski | 1.1.0 | 2025112 | port | - | Phase 4 extras; Roborovski API client |
| sealed-messages | 1.2.0 | 8fc32b0 | port | done 1.2.0 | folded into protocol_esr; AES byte parity proven against the npm package (README mentions Shamir but src only seals/unseals) |
| session | 1.6.1 | 467f049 | port | done 1.6.1 | kit/session/login/transact/storage/ui/wallet/account-creation; tests green on recorded fixtures (ContractKit cases deferred to the contract kit) |
| signing-request | 3.4.0 | 18a13f7 | port | done 3.4.0 | request.ts and misc.ts green with exact URI/digest/proof vectors |
| skill | - | f87fb83 | skip | - | Claude Code skill for wharfkit development, no library code |
| starter | - | 253797a | reference | - | starter kit |
| svelte-components | 0.7.0 | e338750 | skip | - | web UI components |
| token | 1.2.0 | 14c6779 | port | done 1.2.0 | Token + system_token embedded-ABI contract; symbol-bound fixtures re-keyed (stale-era recordings) |
| transact-plugin-autocorrect | 1.4.1 | 6d4db2f | port | done 1.4.1 | upstream tests are fully commented out; construction and getException covered |
| transact-plugin-cosigner | 1.1.0 | 7125c53 | port | done 1.1.0 | recorded tx id reproduced byte-exact; live-network "foo" test skipped |
| transact-plugin-explorerlink | 1.0.1 | 6837e47 | port | done 1.0.1 | |
| transact-plugin-finality-callback | 1.0.0 | c1ba8ce | port | done 1.0.0 | blocking waits with configurable delays |
| transact-plugin-finality-checker | 1.0.0 | 9a70f93 | port | done 1.0.0 | ships no tests upstream |
| transact-plugin-mock | 1.1.0 | 83e9be1 | port | done 1.1.0 | tests/util mock plugins (with mock-data) |
| transact-plugin-resource-provider | 1.2.0 | 92d8637 | port | done 1.2.0 | filtering + plugin tests on recorded fixtures; RAM fees priced via the resources module |
| transact-plugin-template | 1.0.0 | 215ee56 | reference | - | template; ported as examples/ |
| tutorial-client | 0.0.0 | 26fcb26 | reference | - | tutorial app |
| tutorial-todo-contract | - | 17f1ce6 | reference | - | sample contract; dkgen golden-test candidate |
| ui-plugin-template | 0.1.0 | 0906118 | reference | - | template |
| wallet-plugin-anchor | 1.7.3 | c460bc2 | port | done 1.7.3 | native+web transports; popups/mode chooser become openLink hook + option-driven mode (DIVERGENCES) |
| wallet-plugin-cleos | 1.2.0 | 3c86de8 | port | done 1.2.0 | dev tool; clipboard button label-only |
| wallet-plugin-cloudwallet | 1.6.5 | 5017944 | port | done 1.6.5 | popup/postMessage becomes WebViewBridge; login-and-sign covered with a scripted bridge |
| wallet-plugin-gatewallet | 1.1.0 | d59fa2a | skip | - | Scatter browser protocol |
| wallet-plugin-imtoken | 1.1.0 | 51f1d3d | skip | - | Scatter browser protocol |
| wallet-plugin-ledger | 0.1.0 | a468070 | skip | - | WebUSB/WebHID transport; revisit only if native HID is demanded |
| wallet-plugin-metamask | 1.2.1 | e0d37ea | skip | - | MetaMask browser extension |
| wallet-plugin-mimic | 1.1.0 | 81ac40d | skip | - | unmodified template fork (class still WalletPluginTEMPLATE) |
| wallet-plugin-mock | 1.1.0 | ac5fb00 | port | - | Phase 3 |
| wallet-plugin-paycash | 1.1.0 | e6c2ada | port | - | Phase 5; ESR-based like Anchor |
| wallet-plugin-privatekey | 1.1.0 | f611111 | port | done 1.1.0 | plugins/wallet/privatekey |
| wallet-plugin-scatter | 1.5.1 | 2d7848d | skip | - | browser |
| wallet-plugin-template | 1.1.0 | 7dac61f | reference | - | template; informs examples/ |
| wallet-plugin-tokenpocket | 1.6.3 | 500ddb9 | skip | - | browser |
| wallet-plugin-web-authenticator | 0.5.1 | 2a17393 | skip | - | browser popup WebAuthn UI |
| wallet-plugin-wombat | 1.5.1 | 96e27be | skip | - | browser |
| web-renderer | 1.4.3 | d6464fb | reference | - | prompt semantics for UserInterface |
| web-ui | 0.4.0 | a9b4c5d | reference | - | web |
| webauthn | 1.3.0 | bb2bdde | reference | - | browser-side WA key/signature creation; informs WA verification in antelope |
| website | 0.0.1 | 1293679 | skip | - | marketing site |
| wharfkit-godot | - | ee5e242 | reference | - | existing GDScript integration; informs the Godot adapter API |
| wharfkit-godot-wallet-plugin-anchor | - | bd4d07d | reference | - | same |
| wharfkit-rs | - | 22f2f27 | reference | - | Rust port; cross-check |

## Third-party (build dependencies, not ports)

See third_party/README.md for versions, sources and local patches.
