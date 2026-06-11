#include <Accord.h>

#include <cstdio>

uint32_t accordTestMillis = 0;

namespace {
int failureCount = 0;

void expect(bool condition, const char *message) {
	if (condition) {
		return;
	}
	std::printf("FAIL: %s\n", message);
	failureCount++;
}

AccordConfig testConfig(size_t maxSubscribers = 4) {
	AccordConfig config;
	config.defaultTimeoutMs = 10000;
	config.maxRetries = 2;
	config.minDeferMs = 10;
	config.defaultDeferMs = 50;
	config.maxDeferMs = 100;
	config.maxSubscribers = maxSubscribers;
	return config;
}

void testCancelStopsStaleCallbacks() {
	accordTestMillis = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "cancel test init");

	bool secondCalled = false;
	accord.onRequest([&accord](AccordRequest &) {
		accord.cancel();
	});
	accord.onRequest([&secondCalled](AccordRequest &request) {
		secondCalled = true;
		request.allow();
	});

	AccordResult result = accord.request("cancel");
	AccordRequestInfo info;
	expect(!result && result.error == AccordError::Cancelled, "cancel request returns cancelled");
	expect(!secondCalled, "subscriber after cancel is not called");
	expect(!accord.getRequestInfo(info), "cancelled request is no longer active");
	expect(accord.getLastFinishState() == AccordState::Cancelled, "last state is cancelled");
	expect(accord.getLastError() == AccordError::Cancelled, "last error is cancelled");
}

void testUnsubscribeStopsSnapshotCallback() {
	accordTestMillis = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "unsubscribe test init");

	bool secondCalled = false;
	AccordSubscription second;
	accord.onRequest([&second](AccordRequest &request) {
		second.unsubscribe();
		request.allow();
	});
	second = accord.onRequest([&secondCalled](AccordRequest &request) {
		secondCalled = true;
		request.allow();
	});

	AccordResult result = accord.request("unsubscribe");
	expect(result.ok, "unsubscribe during snapshot still completes");
	expect(!secondCalled, "unsubscribed snapshot callback is not called");
	expect(accord.getLastFinishState() == AccordState::Ready, "unsubscribe test finishes ready");
}

void testDeinitStopsSnapshotCallback() {
	accordTestMillis = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "deinit test init");

	bool secondCalled = false;
	accord.onRequest([&accord](AccordRequest &) {
		accord.deinit();
	});
	accord.onRequest([&secondCalled](AccordRequest &request) {
		secondCalled = true;
		request.allow();
	});

	AccordResult result = accord.request("deinit");
	expect(!result && result.error == AccordError::Cancelled, "deinit during request returns cancelled");
	expect(!secondCalled, "subscriber after deinit is not called");
	expect(accord.getLastFinishState() == AccordState::Cancelled, "deinit records cancelled state");
}

void testLastResults() {
	accordTestMillis = 0;
	AccordRequestInfo info;

	{
		Accord accord;
		expect(accord.init(testConfig()).ok, "ready init");
		accord.onRequest([](AccordRequest &request) {
			request.allow();
		});
		expect(accord.request("ready").ok, "ready request");
		expect(!accord.getRequestInfo(info), "ready request is no longer active");
		expect(accord.getLastRequestInfo(info), "ready last info exists");
		expect(info.label != nullptr && info.label[0] == 'r', "ready label retained");
		expect(accord.getLastFinishState() == AccordState::Ready, "ready last state");
		expect(accord.getLastError() == AccordError::None, "ready last error");
	}

	{
		Accord accord;
		expect(accord.init(testConfig()).ok, "reject init");
		accord.onRequest([](AccordRequest &request) {
			request.reject("busy");
		});
		expect(accord.request("reject").ok, "reject request completes");
		expect(accord.getLastRequestInfo(info), "reject last info exists");
		expect(info.rejectMessage != nullptr && info.rejectMessage[0] == 'b', "reject message retained");
		expect(accord.getLastFinishState() == AccordState::Rejected, "reject last state");
		expect(accord.getLastError() == AccordError::None, "reject last error");
	}

	{
		Accord accord;
		expect(accord.init(testConfig()).ok, "missing vote init");
		accord.onRequest([](AccordRequest &) {
		});
		AccordResult result = accord.request("missing");
		expect(!result && result.error == AccordError::MissingVote, "missing vote fails");
		expect(accord.getLastFinishState() == AccordState::Failed, "missing vote last state");
		expect(accord.getLastError() == AccordError::MissingVote, "missing vote last error");
	}
}

void testDeferClamps() {
	accordTestMillis = 0;

	uint32_t deferredMs = 0;
	int voteCount = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "defer default init");
	accord.onDeferred([&deferredMs](const AccordRequestInfo &, uint32_t retryAfterMs) {
		deferredMs = retryAfterMs;
	});
	accord.onRequest([&voteCount](AccordRequest &request) {
		voteCount++;
		if (voteCount == 1) {
			request.defer(0);
			return;
		}
		request.allow();
	});

	expect(accord.request("default-defer").ok, "default defer request accepted");
	expect(deferredMs == 50, "defer zero uses default");
	accordTestMillis = 49;
	accord.loop();
	expect(voteCount == 1, "default defer waits until due");
	accordTestMillis = 50;
	accord.loop();
	expect(voteCount == 2, "default defer retries when due");
	expect(accord.getLastFinishState() == AccordState::Ready, "default defer finishes ready");

	auto expectSingleDefer = [](uint32_t requestedMs, uint32_t expectedMs, const char *message) {
		accordTestMillis = 0;
		uint32_t observedMs = 0;
		Accord local;
		expect(local.init(testConfig()).ok, "single defer init");
		local.onDeferred([&observedMs](const AccordRequestInfo &, uint32_t retryAfterMs) {
			observedMs = retryAfterMs;
		});
		local.onRequest([requestedMs](AccordRequest &request) {
			request.defer(requestedMs);
		});
		expect(local.request("single-defer").ok, "single defer request accepted");
		expect(observedMs == expectedMs, message);
	};

	expectSingleDefer(1, 10, "defer clamps to min");
	expectSingleDefer(1000, 100, "defer clamps to max");
}

void testInvalidDeferConfig() {
	AccordConfig config = testConfig();
	config.minDeferMs = 200;
	config.defaultDeferMs = 100;

	Accord accord;
	AccordResult result = accord.init(config);
	expect(!result && result.error == AccordError::InvalidConfig, "invalid defer config rejected");
}

void testSubscriptionResult() {
	Accord accord;

	AccordSubscriptionResult notInitialized =
	    accord.subscribe([](AccordRequest &request) {
		    request.allow();
	    });
	expect(!notInitialized && notInitialized.error == AccordError::NotInitialized,
	    "subscribe reports not initialized");

	expect(accord.init(testConfig(1)).ok, "subscription result init");

	AccordSubscriptionResult nullCallback = accord.subscribe(AccordRequestCallback());
	expect(!nullCallback && nullCallback.error == AccordError::InvalidArgument,
	    "subscribe reports null callback");

	AccordSubscriptionResult first = accord.subscribe([](AccordRequest &request) {
		request.allow();
	});
	expect(first.ok, "first subscription succeeds");

	AccordSubscriptionResult second = accord.subscribe([](AccordRequest &request) {
		request.allow();
	});
	expect(!second && second.error == AccordError::SubscriberLimitReached,
	    "subscribe reports subscriber limit");

	AccordSubscription empty = accord.onRequest([](AccordRequest &request) {
		request.allow();
	});
	expect(!empty, "onRequest keeps empty handle compatibility");
}
} // namespace

int main() {
	testCancelStopsStaleCallbacks();
	testUnsubscribeStopsSnapshotCallback();
	testDeinitStopsSnapshotCallback();
	testLastResults();
	testDeferClamps();
	testInvalidDeferConfig();
	testSubscriptionResult();

	if (failureCount != 0) {
		std::printf("%d host tests failed\n", failureCount);
		return 1;
	}

	std::printf("host tests passed\n");
	return 0;
}
