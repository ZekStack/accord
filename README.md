# Accord

Accord is a reboot coordination library for ESP32.

Accord helps you safely coordinate `ESP.restart()` in Arduino ESP32 projects. It is designed for applications where modules may need to finish flash writes, OTA work, hardware operations, sync jobs, or user actions before rebooting.

[![CI](https://github.com/ZekStack/accord/actions/workflows/ci.yml/badge.svg)](https://github.com/ZekStack/accord/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ZekStack/accord?sort=semver)](https://github.com/ZekStack/accord/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Why use Accord?

* **Coordinated reboot** - modules vote before user code calls `ESP.restart()`.
* **Small API** - request, allow, reject, defer, cancel, and strict force flows.
* **ESP32-friendly** - fixed subscriber and vote-snapshot storage after `init()`.
* **Thread-safe internals** - state is guarded by a FreeRTOS recursive mutex.
* **Callback-safe teardown** - external cancel, unsubscribe, and deinit operations prevent later callback admission and synchronize with in-flight callbacks.
* **Production-minded** - result-based errors, retry limits, timeout handling, and explicit callback rules.

## Install

### PlatformIO

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

lib_deps =
  https://github.com/ZekStack/accord.git

build_flags =
  -std=gnu++20
build_unflags =
  -std=gnu++11
```

### Arduino IDE

Accord is not published to Arduino Library Manager yet.

Install it by downloading the repository ZIP or cloning it into your Arduino libraries folder.

```txt
Arduino/libraries/Accord
```

## Quick start

```cpp
#include <Arduino.h>
#include <Accord.h>

Accord accord;

void setup() {
	Serial.begin(115200);

	AccordResult initResult = accord.init();
	if (!initResult) {
		Serial.println(initResult.message);
		return;
	}

	accord.onRequest([](AccordRequest &request) {
		request.allow();
	});

	accord.onReady([](const AccordRequestInfo &info) {
		Serial.printf("ready to reboot: %s\n", info.label);
		// ESP.restart();
	});

	accord.request("manual-reboot");
}

void loop() {
	accord.loop();

	// Your normal application loop work here.
	delay(10);
}
```

## Important notes

> [!IMPORTANT]
> Accord does not reboot by itself. `onReady()` is a notification; user code must call `ESP.restart()` when appropriate.

* A successful `request()` means Accord accepted and processed the request. It does not mean reboot is approved. Call `ESP.restart()` only from `onReady()` or after checking `getLastFinishState() == AccordState::Ready`.
* `getState()` reports the live state. Terminal states remain visible while their lifecycle callback executes, then Accord returns to `Idle`.
* Completed outcomes are retained through `getLastRequestInfo()`, `getLastError()`, and `getLastFinishState()`.
* A rejected request is a normal vote outcome, not an error. For rejected requests, `getLastError()` returns `None`; use `getLastRequestInfo(info).rejectMessage` for the rejection reason.
* `force()` skips voting, but only when Accord is idle. It does not interrupt an active request.
* Subscriber callbacks and lifecycle callbacks run without the state mutex held.
* Accord serializes user callbacks per instance through a separate recursive callback gate.
* Each `AccordRequest` accepts one decision. If a subscriber votes more than once, the first vote wins.
* Subscription handles unsubscribe only when `unsubscribe()` is called explicitly.
* Assigning over a live subscription handle releases that handle without unsubscribing the old subscriber.
* Handles created before `deinit()` are generation-bound and cannot remove subscribers created after reinitialization.
* Callback storage uses `std::function`; registering callbacks with larger captures may allocate.
* Labels and reject messages are borrowed `const char*` pointers. Their storage must remain valid until the next request completes, Accord is reinitialized, or Accord is destroyed.

## Cross-task behavior

External operations use the following guarantees:

* `cancel()` invalidates the request before waiting for any currently executing subscriber callback. No later subscriber callback from that request can begin after cancellation is observed.
* `unsubscribe()` marks the subscriber inactive before waiting for an in-flight invocation to exit. When it returns, that subscription cannot begin another callback.
* `deinit()` blocks new Accord work, waits for in-flight callbacks, then clears callback and subscriber storage before returning.

Calling these operations from inside an Accord callback is supported:

* self-unsubscribe marks the subscription inactive and defers storage cleanup until callback exit;
* self-cancel invalidates the request and discards the current callback's vote;
* self-deinit returns successfully and completes cleanup after the outermost Accord callback exits.

## Examples

| Example | Description |
| --- | --- |
| `Basic` | Minimal request with allowing subscribers. |
| `Reject` | Reject a reboot and report the first reject message. |
| `DeferRetry` | Defer while busy and allow on a later retry. |
| `Cancel` | Cancel an active reboot request. |
| `Force` | Use strict force while Accord is idle. |
| `SubscriptionHandle` | Explicitly unsubscribe a request subscriber. |
| `BindableCallbacks` | Bind private class methods through lambdas. |
| `SelfTest` | Manual serial self-test covering common request outcomes on ESP32. |

Start with:

```txt
examples/Basic
```

## Documentation

Detailed documentation is available in the `docs/` folder.

| Document | Description |
| --- | --- |
| [`docs/getting-started.md`](docs/getting-started.md) | Step-by-step setup and first request flow. |
| [`docs/configuration.md`](docs/configuration.md) | Configuration options and defaults. |
| [`docs/api.md`](docs/api.md) | Public classes, methods, callbacks, states, and errors. |
| [`docs/examples.md`](docs/examples.md) | Explanation of all included examples. |
| [`docs/troubleshooting.md`](docs/troubleshooting.md) | Common behavior notes and fixes. |

## API overview

```cpp
Accord accord;
accord.init();
accord.onRequest([](AccordRequest &request) {
	request.allow();
});
accord.onReady([](const AccordRequestInfo &info) {
	// ESP.restart();
});
accord.request("config-change");
accord.loop();
```

For the full API, see [`docs/api.md`](docs/api.md).

## Compatibility

| Item | Support |
| --- | --- |
| Framework | Arduino ESP32 |
| Platform | `espressif32` |
| Language | C++20 |
| Filesystem | none |
| PSRAM | not used |
| Dependencies | none |
| Exceptions | Not used by Accord |
| Status | Early-stage `0.1.0` |

## Configuration

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

For all options, see [`docs/configuration.md`](docs/configuration.md).

## Error handling

Accord reports operation status through `AccordResult`.

```cpp
AccordResult result = accord.request("ota-update");

if (!result) {
	Serial.println(result.message);
	return;
}
```

A successful `request()` result only means Accord accepted and processed the request. Wait for `onReady()` or check `getLastFinishState() == AccordState::Ready` before rebooting.

For all error codes, see [`docs/api.md`](docs/api.md).

## License

MIT - see [`LICENSE.md`](LICENSE.md).

## ZekStack

Part of the ZekStack ESP32 library stack.
