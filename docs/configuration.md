# Configuration

Configure Accord before registering subscribers.

```cpp
AccordConfig config;
config.defaultTimeoutMs = 30000;
config.maxRetries = 10;
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
| `maxDeferMs` | `60000` | Maximum single defer delay. Longer defer requests are clamped. |
| `allowWithoutSubscribers` | `true` | Allows `onReady()` when no modules subscribed. |
| `maxSubscribers` | `16` | Fixed subscriber storage allocated during `init()`. |

## Memory behavior

Accord allocates fixed subscriber storage during `init()` and does not grow it later. If the subscriber capacity is reached, `onRequest()` returns an empty `AccordSubscription`.

## Thread safety

Public methods are guarded by a FreeRTOS recursive mutex. Subscriber callbacks and completion callbacks are invoked outside the mutex so callbacks can call Accord APIs without deadlocking.
