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

`rejectMessage` stores the first rejection message pointer. It may be `nullptr`.

`getRequestInfo(info)` returns `true` only while a request is active or deferred.

After completion, `getLastRequestInfo(info)` returns the last finished request if one exists. `getLastFinishState()` reports `Ready`, `Rejected`, `Failed`, or `Cancelled`, and `getLastError()` reports the final error code.

`getState()` reports the current active state and usually returns `Idle`, `CollectingVotes`, or `Deferred`. Completed outcomes are available through `getLastFinishState()`.

`Rejected` is a normal vote outcome, not an error. For rejected requests, `getLastError()` returns `AccordError::None`; use `getLastRequestInfo(info).rejectMessage` for the rejection reason.

## Errors

| Error | Meaning |
| --- | --- |
| `None` | No error. |
| `NotInitialized` | Accord was not initialized. |
| `AlreadyInitialized` | `init()` was called twice. |
| `RequestAlreadyActive` | A request is already active. |
| `NoSubscribers` | No subscribers and `allowWithoutSubscribers` is false. |
| `SubscriberLimitReached` | Subscriber storage limit reached. |
| `RequestTimeout` | The request exceeded `defaultTimeoutMs`. |
| `MaxRetriesReached` | A deferred request exceeded `maxRetries`. |
| `MissingVote` | A subscriber returned without voting. |
| `InvalidConfig` | Config values are invalid. |
| `InvalidArgument` | A required argument was invalid. |
| `OutOfMemory` | Allocation failed. |
| `Cancelled` | Request was cancelled or became stale. |
| `SubscriptionNotFound` | Unsubscribe target was not found. |
| `InternalError` | Internal failure. |

## Callback reentrancy

Accord snapshots active subscribers, releases the mutex, checks that the request is still active before each callback, invokes the callback, then re-locks and merges votes only if the request id is still active. Callbacks from cancelled, completed, or deinitialized requests are not invoked after the request stops.

Completion callbacks are also invoked outside the mutex.

## Host logic tests

The host logic tests compile Accord with Arduino and FreeRTOS stubs. They validate request control flow, vote merging, and public diagnostics, but they do not model real ESP32 mutex contention, deletion timing, or hardware scheduling behavior.
