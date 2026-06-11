#include <Arduino.h>
#include <Accord.h>

Accord accord;

class RebootGuard {
  public:
	void begin() {
		_subscription = accord.onRequest([this](AccordRequest &request) {
			handleRequest(request);
		});
	}

  private:
	void handleRequest(AccordRequest &request) {
		if (_busy) {
			request.reject("guard busy");
			return;
		}
		request.allow();
	}

	bool _busy = false;
	AccordSubscription _subscription;
};

RebootGuard guard;

void setup() {
	Serial.begin(115200);
	delay(200);

	AccordResult initResult = accord.init();
	if (!initResult) {
		Serial.println(initResult.message);
		return;
	}

	guard.begin();

	accord.onReady([](const AccordRequestInfo &info) {
		Serial.printf("ready: %s\n", info.label);
	});

	accord.request("manual-reboot");
}

void loop() {
	accord.loop();
	delay(10);
}
