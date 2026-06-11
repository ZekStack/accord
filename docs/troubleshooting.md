# Troubleshooting

## `onFailed()` reports `MissingVote`

Every active subscriber must call exactly one vote method. If a callback returns without `allow()`, `reject()`, or `defer()`, Accord fails the request.

## A second vote did nothing

Each `AccordRequest` accepts only one decision. The first vote wins and later votes from the same subscriber callback are ignored.

## `force()` returns `RequestAlreadyActive`

Force is strict in v0.1. It skips voting only when Accord is idle and does not interrupt active requests. Cancel the active request first if that is the desired application behavior.

## `getRequestInfo()` returns false

Request info is available only while a request is active or deferred. After `onReady()`, `onRejected()`, `onFailed()`, or `onCancelled()` finishes, Accord returns to idle.

## Reject message looks corrupted

Reject messages are stored as `const char*`. Use string literals or other stable storage.

```cpp
request.reject("database dirty");
```

Avoid temporary strings:

```cpp
String message = "busy";
request.reject(message.c_str());
```

## Deferred request never retries

Call `accord.loop()` regularly. Accord v0.1 does not create a task; deferred retry and timeout checks are driven by `loop()`.
