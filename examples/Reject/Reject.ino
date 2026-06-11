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
		request.allow();
	});

	accord.onRequest([](AccordRequest &request) {
		request.reject("flash write active");
	});

	accord.onRejected([](const AccordRequestInfo &info) {
		Serial.printf(
		    "rejected count=%u reason=%s\n",
		    static_cast<unsigned>(info.rejectCount),
		    info.rejectMessage != nullptr ? info.rejectMessage : "none"
		);
	});

	accord.request("config-change");
}

void loop() {
	accord.loop();
	delay(10);
}
