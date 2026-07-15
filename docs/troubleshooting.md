# Troubleshooting

## `onFailed()` reports `MissingVote`

Every active subscriber must call exactly one vote method. If a callback returns without `allow()`, `reject()`, or `defer()`, Accord fails the request.

A subscriber removed while its callback is running is no longer required to vote; its result is discarded.

## A second vote did nothing

Each `AccordRequest` accepts only one decision. The first vote wins and later votes from the same subscriber callback are ignored.

## `force()` returns `RequestAlreadyActive`

Force is strict in v0.1. It skips voting only when Accord is idle and does not interrupt active requests or terminal lifecycle callbacks. Cancel the active request first if that is the desired application behavior.

## `getRequestInfo()` returns false

Request info is available only while a request is collecting votes or deferred.

During `onReady()`, `onRejected()`, `onFailed()`, or `onCancelled()`, `getState()` reports the terminal state, while completed diagnostics are already available through `getLastRequestInfo()`.

## Reject message or label looks corrupted

Labels and reject messages are borrowed `const char*` pointers. Accord does not copy their contents.

Use string literals, static storage, or buffers that remain valid until the next request completes, Accord is reinitialized, or Accord is destroyed.

```cpp
accord.request("database-maintenance");
request.reject("database dirty");
```

Avoid temporary strings:

```cpp
String message = "busy";
request.reject(message.c_str());
```

## Deferred request never retries

Call `accord.loop()` regularly. Accord v0.1 does not create a task; deferred retry and timeout checks are driven by `loop()`.

## A long subscriber callback ended with `RequestTimeout`

`defaultTimeoutMs` covers the entire request, including time spent inside subscriber callbacks. Accord checks the deadline immediately after each callback and discards a vote returned after the timeout.

Increase the timeout or change the module to call `defer()` quickly and finish its work asynchronously.

## An old subscription handle returns `SubscriptionNotFound`

Subscription handles are tied to the `init()` generation that created them. After `deinit()` and a later `init()`, old handles are intentionally stale and cannot affect new subscribers.

## `deinit()` blocks in another task

External `deinit()` waits for an in-flight Accord callback to finish before clearing callback and subscriber storage. This prevents use-after-free when callbacks capture module state.

Calling `deinit()` from inside an Accord callback does not block on itself. Cleanup is deferred until the outermost Accord callback returns.

## Can callbacks execute concurrently?

Accord serializes subscriber and lifecycle callbacks per instance. Application tasks may call Accord concurrently, but only one Accord callback for a given instance executes at a time.
