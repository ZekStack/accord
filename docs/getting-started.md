# Getting started

Accord coordinates reboot requests. It does not reboot the ESP32 by itself.

## Basic setup

```cpp
#include <Arduino.h>
#include <Accord.h>

Accord accord;

void setup() {
	Serial.begin(115200);

	AccordResult result = accord.init();
	if (!result) {
		Serial.println(result.message);
		return;
	}

	accord.onRequest([](AccordRequest &request) {
		request.allow();
	});

	accord.onReady([](const AccordRequestInfo &info) {
		Serial.printf("ready: %s\n", info.label);
		// ESP.restart();
	});

	accord.request("manual-reboot");
}

void loop() {
	accord.loop();
	delay(10);
}
```

## Request flow

```txt
request(label)
  -> notify subscribers
  -> collect one vote from each subscriber
  -> reject beats defer, defer beats allow
  -> all allow: onReady()
  -> any reject: onRejected()
  -> any defer: onDeferred(), then retry from loop()
  -> timeout or missing vote: onFailed()
```

After `onReady()` returns, the request is finished and Accord returns to idle. The application decides whether to call `ESP.restart()`.

## Labels

Labels should describe why the reboot is needed:

```cpp
accord.request("ota-update");
accord.request("factory-reset");
accord.request("config-change");
accord.request("manual-reboot");
```

Accord stores the label pointer as `const char*`. Prefer string literals or other stable storage.
