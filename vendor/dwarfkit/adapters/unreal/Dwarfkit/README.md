# Dwarfkit Unreal Plugin

Thin adapter over the dwarfkit static library (BLUEPRINT.md 8.1): the four
interfaces implemented on Unreal subsystems, a GameInstance subsystem that
owns the `SessionKit`, and Blueprint async nodes. No kit logic lives here.

## Building the library

The plugin links a prebuilt `dwarfkit` static library. Build it once per
platform with curl OFF (the adapter supplies FHttp/FWebSockets transports)
and the engine's CRT:

```
cmake -S <dwarfkit repo> -B build-ue -DDK_WITH_CURL=OFF -DDK_BUILD_TESTS=OFF -DDK_BUILD_TOOLS=OFF -DDK_BUILD_EXAMPLES=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
cmake --build build-ue --config Release --target dwarfkit
```

Copy into `Source/ThirdParty/DwarfkitLib/`:

- `include/` <- the repo's `include/` tree plus `third_party/expected/include`
  and `third_party/nlohmann/include` merged under `include/third_party/`
- `lib/Win64/dwarfkit.lib` (or `lib/<Platform>/libdwarfkit.a`)

## Pieces

- `FDkUnrealFetchProvider` - FHttpModule request; the kit worker blocks on an
  FEvent until the HTTP callback completes.
- `FDkUnrealWebSocketProvider` - FWebSocketsModule socket with a frame queue;
  `receive` blocks the worker with the requested timeout and cancel token.
- `FDkUnrealStorage` / `FileSessionStorage` under
  `<ProjectSaved>/Dwarfkit/`.
- `FDkUnrealUserInterface` - forwards prompts/status to a Blueprint
  `UDwarfkitUI` object on the game thread (QR data arrives as the element's
  JSON payload; render it with any QR widget).
- `UDwarfkitSubsystem` - owns the kit; `Configure(AppName, ChainId, Url)`
  wires WalletPluginAnchor over the Unreal websocket transport.
- Async nodes: `Login`, `Restore`, `Transact` (single action as JSON).

## Sample map

Drop `ADwarfkitSampleActor` into an empty level. On BeginPlay it configures
the subsystem for Jungle 4, starts an Anchor login (prompting through the
assigned `UDwarfkitUI`; the QR element's data is the `esr:` payload to
render), then broadcasts a 0.0001 EOS transfer and logs the transaction id.

Threading rule: never call a kit method on the game thread. The async nodes
and the sample follow this; if you call the kit directly, do it from a worker
and marshal results back yourself.
