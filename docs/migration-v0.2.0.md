# Migrating to Accord v0.2.0

Accord v0.2.0 migrates its explicit memory and synchronization ownership to Strata.

The coordination model is unchanged: request, vote, reject, defer, cancel, force, subscription lifetime, callback ordering, timeout behavior, and deferred retry semantics remain the same as v0.1.0.

## New dependency

Accord v0.2.0 requires Strata v0.1.2.

PlatformIO users should allow Accord's `library.json` dependency to resolve Strata or pin it explicitly:

```ini
lib_deps =
  https://github.com/ZekStack/accord.git#v0.2.0
  https://github.com/ZekStack/strata.git#v0.1.2
```

Arduino IDE users must install both libraries.

## New memory policy

`AccordConfig` now includes the shared Strata memory policy:

```cpp
AccordConfig config;
config.memory.allocation = Strata::Placement::Default;
accord.init(config);
```

`memory.allocation` controls the fixed subscriber table and vote snapshot allocated during `init()`.

The `AccordImpl` object and recursive-mutex control blocks remain in internal memory. `memory.taskStack` is present because Accord uses the common `Strata::MemoryPolicy` type, but it is currently unused because Accord owns no FreeRTOS tasks.

## Placement behavior

`Strata::Placement::Default` preserves the default allocation behavior expected by v0.1.0 users.

Use `Internal` when Accord's tables must stay in internal RAM. Use `PreferExternal` when external memory should be preferred where available. `RequireExternal` makes initialization fail with `AccordError::OutOfMemory` when external memory cannot satisfy the table allocation.

## API compatibility

No existing Accord method or callback signature was removed or renamed.

Appending `memory` to `AccordConfig` preserves the existing field order, so aggregate initialization that supplies only the original v0.1.0 fields continues to initialize the new memory policy from its defaults.

## Callback allocations

`std::function` remains the callback type. Large callback captures may still allocate through the C++ standard library. The v0.2.0 migration covers Accord's explicit owned storage and owned FreeRTOS synchronization primitives; it does not replace the public callback abstraction.
