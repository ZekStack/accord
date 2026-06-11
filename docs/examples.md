# Examples

## Basic

Minimal initialization, one allowing subscriber, and `onReady()`.

## Reject

Shows a subscriber blocking reboot with a stable string-literal reason. `onRejected()` receives the first reject message and reject count.

## DeferRetry

Shows a busy module deferring the request. `loop()` retries all subscribers after the longest defer delay.

## Cancel

Starts a request and cancels it. `onCancelled()` receives the request info captured before Accord returns to idle.

## Force

Uses `force("critical-watchdog")` while Accord is idle. Force skips voting but does not interrupt an active request.

## SubscriptionHandle

Stores the handle returned from `onRequest()` and explicitly unsubscribes it.

## BindableCallbacks

Uses a class with private methods bound through lambdas. Accord callbacks are `std::function` based, so lambdas, captures, and `std::bind` style callbacks are supported.

## SelfTest

Manual ESP32 serial self-test. It prints PASS/FAIL results for allow, reject, defer retry, cancel during callback, unsubscribe during callback, no-subscriber behavior, and timeout behavior. It does not reboot the device.
