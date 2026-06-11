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

Accord snapshots active subscribers, releases the mutex, invokes callbacks, then re-locks and merges votes only if the request id is still active. Votes from cancelled, completed, or deinitialized requests are ignored.

Completion callbacks are also invoked outside the mutex.
