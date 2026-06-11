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
		request.defer(5000);
	});

	accord.onCancelled([](const AccordRequestInfo &info) {
		Serial.printf("cancelled: %s\n", info.label);
	});

	accord.request("manual-reboot");
	accord.cancel();
}

void loop() {
	accord.loop();
	delay(10);
}
