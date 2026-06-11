#include <Arduino.h>
#include <Accord.h>

namespace {
uint32_t passedCount = 0;
uint32_t failedCount = 0;

void printResult(const char *name, bool passed) {
	Serial.printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
	if (passed) {
		passedCount++;
		return;
	}
	failedCount++;
}

void printSummary() {
	Serial.printf(
	    "SelfTest complete: passed=%u failed=%u\n",
	    static_cast<unsigned>(passedCount),
	    static_cast<unsigned>(failedCount)
	);
}

AccordConfig selfTestConfig() {
	AccordConfig config;
	config.defaultTimeoutMs = 100;
	config.maxRetries = 3;
	config.minDeferMs = 10;
	config.defaultDeferMs = 20;
	config.maxDeferMs = 50;
	config.maxSubscribers = 4;
	return config;
}

void runAllowFlow() {
	Accord accord;
	bool readyCalled = false;

	AccordResult initResult = accord.init(selfTestConfig());
	if (!initResult) {
		printResult("allow init", false);
		return;
	}

	accord.onRequest([](AccordRequest &request) {
		request.allow();
	});
	accord.onReady([&readyCalled](const AccordRequestInfo &) {
		readyCalled = true;
	});

	AccordResult result = accord.request("selftest-allow");
	printResult("allow flow", result.ok && readyCalled &&
	                              accord.getLastFinishState() == AccordState::Ready);
}

void runRejectFlow() {
	Accord accord;
	bool rejectedCalled = false;
	const char *reason = nullptr;

	AccordResult initResult = accord.init(selfTestConfig());
	if (!initResult) {
		printResult("reject init", false);
		return;
	}

	accord.onRequest([](AccordRequest &request) {
		request.reject("selftest reject");
	});
	accord.onRejected([&rejectedCalled, &reason](const AccordRequestInfo &info) {
		rejectedCalled = true;
		reason = info.rejectMessage;
	});

	AccordResult result = accord.request("selftest-reject");
	printResult("reject flow",
	    result.ok && rejectedCalled && reason != nullptr &&
	        accord.getLastFinishState() == AccordState::Rejected &&
	        accord.getLastError() == AccordError::None);
}

void runDeferRetryFlow() {
	Accord accord;
	uint32_t voteCount = 0;
	bool deferredCalled = false;
	bool readyCalled = false;

	AccordResult initResult = accord.init(selfTestConfig());
	if (!initResult) {
		printResult("defer retry init", false);
		return;
	}

	accord.onRequest([&voteCount](AccordRequest &request) {
		voteCount++;
		if (voteCount == 1) {
			request.defer(20);
			return;
		}
		request.allow();
	});
	accord.onDeferred([&deferredCalled](const AccordRequestInfo &, uint32_t) {
		deferredCalled = true;
	});
	accord.onReady([&readyCalled](const AccordRequestInfo &) {
		readyCalled = true;
	});

	AccordResult result = accord.request("selftest-defer");
	const uint32_t startMs = millis();
	while (static_cast<int32_t>(millis() - startMs) < 200 && accord.isRequestActive()) {
		accord.loop();
		delay(1);
	}

	printResult("defer retry flow", result.ok && deferredCalled && readyCalled && voteCount >= 2 &&
	                                  accord.getLastFinishState() == AccordState::Ready);
}

void runCancelDuringCallbackFlow() {
	Accord accord;
	bool secondCalled = false;

	AccordResult initResult = accord.init(selfTestConfig());
	if (!initResult) {
		printResult("cancel callback init", false);
		return;
	}

	accord.onRequest([&accord](AccordRequest &) {
		accord.cancel();
	});
	accord.onRequest([&secondCalled](AccordRequest &request) {
		secondCalled = true;
		request.allow();
	});

	AccordResult result = accord.request("selftest-cancel");
	printResult("cancel during callback",
	    !result.ok && result.error == AccordError::Cancelled && !secondCalled &&
	        accord.getLastFinishState() == AccordState::Cancelled);
}

void runUnsubscribeDuringCallbackFlow() {
	Accord accord;
	AccordSubscription second;
	bool secondCalled = false;

	AccordResult initResult = accord.init(selfTestConfig());
	if (!initResult) {
		printResult("unsubscribe callback init", false);
		return;
	}

	accord.onRequest([&second](AccordRequest &request) {
		second.unsubscribe();
		request.allow();
	});
	second = accord.onRequest([&secondCalled](AccordRequest &request) {
		secondCalled = true;
		request.allow();
	});

	AccordResult result = accord.request("selftest-unsubscribe");
	printResult("unsubscribe during callback",
	    result.ok && !secondCalled && accord.getLastFinishState() == AccordState::Ready);
}

void runNoSubscriberFlow() {
	Accord accord;
	AccordConfig config = selfTestConfig();
	config.allowWithoutSubscribers = false;

	AccordResult initResult = accord.init(config);
	if (!initResult) {
		printResult("no subscriber init", false);
		return;
	}

	AccordResult result = accord.request("selftest-no-subscriber");
	printResult("no subscriber flow",
	    !result.ok && result.error == AccordError::NoSubscribers &&
	        accord.getLastFinishState() == AccordState::Failed &&
	        accord.getLastError() == AccordError::NoSubscribers);
}

void runTimeoutFlow() {
	Accord accord;
	AccordConfig config = selfTestConfig();
	config.defaultTimeoutMs = 20;

	AccordResult initResult = accord.init(config);
	if (!initResult) {
		printResult("timeout init", false);
		return;
	}

	accord.onRequest([](AccordRequest &request) {
		request.defer(50);
	});

	AccordResult result = accord.request("selftest-timeout");
	const uint32_t startMs = millis();
	while (static_cast<int32_t>(millis() - startMs) < 200 && accord.isRequestActive()) {
		accord.loop();
		delay(1);
	}

	printResult("timeout flow",
	    result.ok && accord.getLastFinishState() == AccordState::Failed &&
	        accord.getLastError() == AccordError::RequestTimeout);
}
} // namespace

void setup() {
	Serial.begin(115200);
	delay(200);

	Serial.println("Accord SelfTest starting");
	runAllowFlow();
	runRejectFlow();
	runDeferRetryFlow();
	runCancelDuringCallbackFlow();
	runUnsubscribeDuringCallbackFlow();
	runNoSubscriberFlow();
	runTimeoutFlow();
	printSummary();
}

void loop() {
}
