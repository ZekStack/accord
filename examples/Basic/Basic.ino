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
		Serial.printf("request: %s\n", request.label());
		request.allow();
	});

	accord.onReady([](const AccordRequestInfo &info) {
		Serial.printf("ready to reboot: %s\n", info.label);
	});

	accord.request("manual-reboot");
}

void loop() {
	accord.loop();
	delay(10);
}
