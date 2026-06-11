#include <Arduino.h>
#include <Accord.h>

Accord accord;
AccordSubscription subscription;

void setup() {
	Serial.begin(115200);
	delay(200);

	AccordResult initResult = accord.init();
	if (!initResult) {
		Serial.println(initResult.message);
		return;
	}

	subscription = accord.onRequest([](AccordRequest &request) {
		Serial.println("subscriber voted");
		request.allow();
	});

	accord.onReady([](const AccordRequestInfo &info) {
		Serial.printf("ready: %s subscribers=%u\n", info.label, static_cast<unsigned>(info.subscriberCount));
	});

	accord.request("before-unsubscribe");
	subscription.unsubscribe();
	accord.request("after-unsubscribe");
}

void loop() {
	accord.loop();
	delay(10);
}
