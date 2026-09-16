# Configuration

Configure Accord before registering subscribers.

```cpp
AccordConfig config;
config.defaultTimeoutMs = 30000;
config.maxRetries = 10;
config.minDeferMs = 10;
config.defaultDeferMs = 1000;
config.maxDeferMs = 60000;
config.allowWithoutSubscribers = true;
config.maxSubscribers = 16;
config.memory.allocation = Strata::Placement::Default;

AccordResult result = accord.init(config);
```

## Options

| Field | Default | Description |
| --- | --- | --- |
| `defaultTimeoutMs` | `30000` | Full active-request timeout, including subscriber callback execution. `0` disables timeout checks. |
| `maxRetries` | `10` | Maximum defer retries before `onFailed()` receives `MaxRetriesReached`. |
| `minDeferMs` | `10` | Minimum single defer delay. Shorter non-zero defer requests are clamped. |
| `defaultDeferMs` | `1000` | Delay used when a subscriber calls `defer(0)`. |
| `maxDeferMs` | `60000` | Maximum single defer delay. Longer defer requests are clamped. |
| `allowWithoutSubscribers` | `true` | Allows `onReady()` when no modules subscribed. |
| `maxSubscribers` | `16` | Fixed subscriber and vote-snapshot capacity allocated during `init()`. |
| `memory.allocation` | `Strata::Placement::Default` | Placement policy for the fixed subscriber table and vote snapshot. |
| `memory.taskStack` | `Strata::Placement::Internal` | Part of the shared Strata memory policy; currently unused because Accord owns no tasks. |

## Memory behavior

Accord v0.2.0 routes its explicit allocation ownership through Strata.

The `AccordImpl` object and both recursive-mutex control blocks are kept in internal memory. The fixed-capacity subscriber table and vote snapshot are allocated during `init()` using `config.memory.allocation` and are released during `deinit()`.

Accord does not allocate a new vote snapshot for each request or retry.

Available allocation placements are:

| Placement | Behavior |
| --- | --- |
| `Strata::Placement::Default` | Use Strata's platform default allocation behavior. |
| `Strata::Placement::Internal` | Require internal memory. |
| `Strata::Placement::PreferExternal` | Prefer external memory and fall back according to Strata's platform policy. |
| `Strata::Placement::RequireExternal` | Require external memory; `init()` returns `OutOfMemory` when that placement cannot be satisfied. |

The complete `Strata::MemoryPolicy` is validated even though Accord does not currently use `memory.taskStack`.

Callback storage uses `std::function`, so registering a callback with a larger capture may allocate through the C++ standard library. This is intentionally outside Accord's explicit fixed-storage ownership migration.

If capacity is reached, `onRequest()` returns an empty `AccordSubscription`. Use `subscribe()` to receive `SubscriberLimitReached` and a failure message.

## Defer bounds and wraparound

`minDeferMs`, `defaultDeferMs`, and `maxDeferMs` must be non-zero and ordered as:

```txt
minDeferMs <= defaultDeferMs <= maxDeferMs
```

Invalid bounds make `init()` fail with `InvalidConfig`.

Deferred readiness uses unsigned elapsed time (`millis() - retryStartedAtMs`) rather than a signed absolute deadline, so normal `millis()` wraparound is handled correctly.

## Thread safety

Public state is guarded by a Strata-owned FreeRTOS recursive mutex. A separate Strata-owned recursive callback gate serializes callback execution with cancel, unsubscribe, and deinit operations.

Callbacks run without the state mutex held and may call Accord APIs reentrantly. See [`api.md`](api.md) for the precise external and self-call guarantees.
