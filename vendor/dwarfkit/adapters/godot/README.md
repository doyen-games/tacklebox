# Dwarfkit Godot GDExtension

Thin adapter over the dwarfkit static library (BLUEPRINT.md 8.2): RefCounted
wrappers, one json <-> Variant conversion point, signal-based async, and a
DkUserInterface base class scripts extend. No kit logic lives here.

## Building

1. Check out godot-cpp (4.3+) into `godot-cpp/`.
2. Build the dwarfkit static library with curl OFF and copy it to
   `lib/<platform>/`:

```
cmake -S <dwarfkit repo> -B build-godot -DDK_WITH_CURL=OFF -DDK_BUILD_TESTS=OFF -DDK_BUILD_TOOLS=OFF -DDK_BUILD_EXAMPLES=OFF
cmake --build build-godot --config Release --target dwarfkit
```

3. `scons platform=<platform>` (set `DWARFKIT_ROOT` if the repo is elsewhere).
   The shared library lands in `demo/bin/`.

## Pieces

- `DkSessionKit` (configure/set_ui/login/restore/logout) with signals
  `login_completed(session)`, `restore_completed(session)`, `error(message)`.
- `DkSession` (get_chain_id/get_actor/get_permission, transact) with signals
  `transact_completed(result)`, `error(message)`.
- `DkUserInterface`: extend in GDScript and override `_prompt(args)`,
  `_status(message)`, `_error(message)`. Calls arrive via call_deferred on the
  main thread; the kit worker waits on a Semaphore until they return.
- Providers: `HTTPClient` and `WebSocketPeer` polled from the worker thread;
  storage under `user://dwarfkit/`.

## Demo

`demo/login_transfer.gd` configures Jungle 4, starts an Anchor login (the QR
`esr:` payload arrives in `_prompt`), then broadcasts a 0.0001 EOS transfer
and prints the transaction id. Attach it to any Control node in a scene saved
as `login_transfer.tscn`.

Threading rule: never call kit methods from the main thread yourself; the
wrappers already run them on workers and marshal results back with signals.
