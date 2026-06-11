#include <Arduino.h>
#include <Accord.h>

Accord accord;

void setup() {
	Serial.begin(115200);
	delay(200);

	AccordResult initResult = accord.init();
	if (!initResult) {
		Serial.println(initResult.message);
		return;
	}

	accord.onRequest([](AccordRequest &request) {
		request.reject("this subscriber is skipped by force");
	});

	accord.onReady([](const AccordRequestInfo &info) {
		Serial.printf("force ready: %s\n", info.label);
	});

	accord.force("critical-watchdog");
}

void loop() {
	accord.loop();
	delay(10);
}
