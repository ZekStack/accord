# API

## Main class

```cpp
class Accord {
public:
	AccordResult init(const AccordConfig &config = AccordConfig());
	AccordResult deinit();

	AccordResult request(const char *label);
	AccordResult cancel();
	AccordResult force(const char *label);

	AccordSubscriptionResult subscribe(AccordRequestCallback callback);
	AccordSubscription onRequest(AccordRequestCallback callback);

	void onReady(AccordReadyCallback callback);
	void onRejected(AccordRejectedCallback callback);
	void onDeferred(AccordDeferredCallback callback);
	void onFailed(AccordFailedCallback callback);
	void onCancelled(AccordCancelledCallback callback);

	void loop();

	bool isInitialized() const;
	bool isRequestActive() const;
	AccordState getState() const;
	bool getRequestInfo(AccordRequestInfo &info) const;
	bool getLastRequestInfo(AccordRequestInfo &info) const;
	AccordError getLastError() const;
	AccordState getLastFinishState() const;

	const char *errorToString(AccordError error) const;
	const char *stateToString(AccordState state) const;
	AccordResult unsubscribe(AccordSubscriptionId subscriptionId);
};
```

## Result

```cpp
struct AccordResult {
	bool ok;
	AccordError error;
	const char *message;

	explicit operator bool() const;
};
```

Use `if (!result)` for error handling.

For `request(label)`, a successful result means Accord accepted and processed the request. It does not mean reboot is approved. Use `onReady()` or check `getLastFinishState() == AccordState::Ready` before user code calls `ESP.restart()`.

## Subscription result

```cpp
struct AccordSubscriptionResult {
	bool ok;
	AccordError error;
	const char *message;
	AccordSubscription subscription;

	explicit operator bool() const;
};
```

Use `subscribe()` when code needs to know why registration failed. `onRequest()` remains the convenience API and returns an empty `AccordSubscription` on failure.

## Request voting

Each subscriber receives a temporary `AccordRequest`.

```cpp
request.allow();
request.reject();
request.reject("flash write active");
request.defer(5000);
```

The first decision on a request object wins. Later decisions from the same subscriber callback are ignored.

A subscriber that returns without voting causes `MissingVote`, unless the subscriber was unsubscribed or the request was cancelled/deinitialized while its callback was running.

## Subscription handles

`onRequest()` returns an `AccordSubscription`. Store it only when you need to unsubscribe later.

```cpp
AccordSubscription subscription = accord.onRequest([](AccordRequest &request) {
	request.allow();
});

subscription.unsubscribe();
```

The handle does not automatically unsubscribe from its destructor, so simple calls such as `accord.onRequest(...)` remain valid.

Move assignment releases the previous handle without unsubscribing it. Call `unsubscribe()` before overwriting a live handle if the old subscriber should be removed.

Handles created by Accord contain an internal initialization generation. A handle retained across `deinit()` cannot unsubscribe a subscriber created after a later `init()`.

`release()` returns only the raw numeric ID. Raw IDs are valid only for the current initialization generation and should not be persisted across `deinit()`.

## Request info

```cpp
struct AccordRequestInfo {
	const char *label;
	const char *rejectMessage;
	uint32_t startedAtMs;
	uint32_t retryCount;
	uint32_t subscriberCount;
	uint32_t allowCount;
	uint32_t rejectCount;
	uint32_t deferCount;
};
```

`getRequestInfo(info)` returns `true` only while a request is collecting votes or deferred.

After completion, `getLastRequestInfo(info)` returns the last finished request if one exists. `getLastFinishState()` reports `Ready`, `Rejected`, `Failed`, or `Cancelled`, and `getLastError()` reports the final error code.

`getState()` reports the live state. Terminal states remain visible while the matching lifecycle callback executes. Accord changes to `Idle` only after that callback returns.

`Rejected` is a normal vote outcome, not an error. For rejected requests, `getLastError()` returns `AccordError::None`; use `getLastRequestInfo(info).rejectMessage` for the rejection reason.

### Borrowed string lifetime

Accord stores `label` and `rejectMessage` as borrowed `const char*` pointers. It does not copy their contents.

The backing storage must remain valid until the next request completes, Accord is reinitialized, or Accord is destroyed. This requirement includes deferred retries and reads through `getLastRequestInfo()`.

Prefer string literals, static storage, or application-owned buffers with a sufficiently long lifetime.

## Errors

| Error | Meaning |
| --- | --- |
| `None` | No error. |
| `NotInitialized` | Accord was not initialized or is deinitializing. |
| `AlreadyInitialized` | `init()` was called twice. |
| `RequestAlreadyActive` | A request or terminal lifecycle callback is active. |
| `NoSubscribers` | No subscribers and `allowWithoutSubscribers` is false. |
| `SubscriberLimitReached` | Subscriber storage limit reached. |
| `RequestTimeout` | The request exceeded `defaultTimeoutMs`, including time spent in subscriber callbacks. |
| `MaxRetriesReached` | A deferred request exceeded `maxRetries`. |
| `MissingVote` | A still-active subscriber returned without voting. |
| `InvalidConfig` | Config values are invalid. |
| `InvalidArgument` | A required argument was invalid. |
| `OutOfMemory` | Initialization storage allocation failed. |
| `Cancelled` | Request was cancelled or became stale. |
| `SubscriptionNotFound` | Unsubscribe target was not found or belonged to an earlier initialization. |
| `InternalError` | Internal synchronization failure. |

## Callback and concurrency contract

Accord uses two recursive FreeRTOS mutexes per instance:

* a state mutex protects request, subscriber, and diagnostic state;
* a callback gate serializes user callback admission and teardown.

The state mutex is not held while user callbacks execute. User callbacks may call Accord APIs reentrantly.

Callbacks are serialized per Accord instance. Two subscriber or lifecycle callbacks from the same instance do not execute concurrently.

### External calls

* `cancel()` invalidates the request first, then synchronizes with the callback gate before delivering `onCancelled()`.
* `unsubscribe()` marks the subscriber inactive first, then waits for an in-flight invocation before returning.
* `deinit()` marks Accord unavailable first, waits for in-flight callbacks, delivers cancellation when needed, and clears callback/subscriber storage before returning.

A callback already executing when cancellation or teardown begins may finish its user code, but its vote is discarded. No later callback from the invalidated request or removed subscriber is admitted.

### Calls from inside callbacks

* self-unsubscribe is supported and defers callback storage cleanup until return;
* self-cancel is supported and invalidates the current request;
* self-deinit is supported and defers final cleanup until the outermost Accord callback returns;
* replacing a lifecycle callback from inside itself is supported; the replacement is installed after the current invocation.

## Timeout behavior

Accord checks the full request timeout:

* before vote snapshot processing;
* before each subscriber callback;
* immediately after each subscriber callback;
* before final vote aggregation;
* from `loop()` while deferred.

A callback that returns after the deadline cannot contribute a vote. The request finishes with `RequestTimeout`, and `onFailed()` is emitted once.

## Host logic tests

The host tests compile Accord with Arduino and FreeRTOS stubs backed by real C++ recursive mutexes. They cover state-machine behavior, threaded deinit/unsubscribe synchronization, stale handles, callback-time timeout, lifecycle ordering, and millis wraparound.

ESP32 builds in CI remain the source of truth for platform compilation and FreeRTOS integration.
