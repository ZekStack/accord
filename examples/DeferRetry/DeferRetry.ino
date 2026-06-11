#include <Arduino.h>
#include <Accord.h>

Accord accord;
bool busy = true;
uint32_t busyUntilMs = 0;

void setup() {
	Serial.begin(115200);
	delay(200);

	AccordConfig config;
	config.maxDeferMs = 2000;

	AccordResult initResult = accord.init(config);
	if (!initResult) {
		Serial.println(initResult.message);
		return;
	}

	accord.onRequest([](AccordRequest &request) {
		if (busy) {
			request.defer(500);
			return;
		}
		request.allow();
	});

	accord.onDeferred([](const AccordRequestInfo &info, uint32_t retryAfterMs) {
		Serial.printf(
		    "deferred %s retry=%u after=%u\n",
		    info.label,
		    static_cast<unsigned>(info.retryCount),
		    static_cast<unsigned>(retryAfterMs)
		);
	});

	accord.onReady([](const AccordRequestInfo &info) {
		Serial.printf("ready after retries=%u\n", static_cast<unsigned>(info.retryCount));
	});

	busyUntilMs = millis() + 1200;
	accord.request("ota-update");
}

void loop() {
	if (busy && static_cast<int32_t>(millis() - busyUntilMs) >= 0) {
		busy = false;
	}
	accord.loop();
	delay(10);
}
