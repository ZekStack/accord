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
| `defaultTimeoutMs` | `30000` | Full active-request timeout. `0` disables timeout checks. |
| `maxRetries` | `10` | Maximum defer retries before `onFailed()` receives `MaxRetriesReached`. |
| `minDeferMs` | `10` | Minimum single defer delay. Shorter non-zero defer requests are clamped. |
| `defaultDeferMs` | `1000` | Delay used when a subscriber calls `defer(0)`. |
| `maxDeferMs` | `60000` | Maximum single defer delay. Longer defer requests are clamped. |
| `allowWithoutSubscribers` | `true` | Allows `onReady()` when no modules subscribed. |
| `maxSubscribers` | `16` | Fixed subscriber storage allocated during `init()`. |

## Memory behavior

Accord allocates fixed subscriber slots during `init()` and does not grow them later. Callback storage uses `std::function`, so small lambdas may fit inline while larger captures may allocate.

If the subscriber capacity is reached, `onRequest()` returns an empty `AccordSubscription`. Use `subscribe()` to receive `SubscriberLimitReached` and a failure message.

## Defer bounds

`minDeferMs`, `defaultDeferMs`, and `maxDeferMs` must be non-zero and ordered as `minDeferMs <= defaultDeferMs <= maxDeferMs`. Invalid bounds make `init()` fail with `InvalidConfig`.

## Thread safety

Public methods are guarded by a FreeRTOS recursive mutex. Subscriber callbacks and completion callbacks are invoked outside the mutex so callbacks can call Accord APIs without deadlocking.
