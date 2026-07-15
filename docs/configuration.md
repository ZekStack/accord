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

## Memory behavior

Accord allocates subscriber records and vote-snapshot keys during `init()` and does not allocate a new snapshot for each request or retry.

Callback storage uses `std::function`, so registering a callback with a larger capture may allocate. Subscriber callback objects are moved into their fixed slots when possible.

If capacity is reached, `onRequest()` returns an empty `AccordSubscription`. Use `subscribe()` to receive `SubscriberLimitReached` and a failure message.

## Defer bounds and wraparound

`minDeferMs`, `defaultDeferMs`, and `maxDeferMs` must be non-zero and ordered as:

```txt
minDeferMs <= defaultDeferMs <= maxDeferMs
```

Invalid bounds make `init()` fail with `InvalidConfig`.

Deferred readiness uses unsigned elapsed time (`millis() - retryStartedAtMs`) rather than a signed absolute deadline, so normal `millis()` wraparound is handled correctly.

## Thread safety

Public state is guarded by a FreeRTOS recursive mutex. A separate recursive callback gate serializes callback execution with cancel, unsubscribe, and deinit operations.

Callbacks run without the state mutex held and may call Accord APIs reentrantly. See [`api.md`](api.md) for the precise external and self-call guarantees.
