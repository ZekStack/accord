#include <Accord.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

std::atomic<uint32_t> accordTestMillis{0};

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
	expect(!accord.isInitialized(), "self deinit completes after callback exits");
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

	AccordSubscriptionResult notInitialized = accord.subscribe([](AccordRequest &request) {
		request.allow();
	});
	expect(
	    !notInitialized && notInitialized.error == AccordError::NotInitialized,
	    "subscribe reports not initialized"
	);

	expect(accord.init(testConfig(1)).ok, "subscription result init");

	AccordSubscriptionResult nullCallback = accord.subscribe(AccordRequestCallback());
	expect(
	    !nullCallback && nullCallback.error == AccordError::InvalidArgument,
	    "subscribe reports null callback"
	);

	AccordSubscriptionResult first = accord.subscribe([](AccordRequest &request) {
		request.allow();
	});
	expect(first.ok, "first subscription succeeds");

	AccordSubscriptionResult second = accord.subscribe([](AccordRequest &request) {
		request.allow();
	});
	expect(
	    !second && second.error == AccordError::SubscriberLimitReached,
	    "subscribe reports subscriber limit"
	);

	AccordSubscription empty = accord.onRequest([](AccordRequest &request) {
		request.allow();
	});
	expect(!empty, "onRequest keeps empty handle compatibility");
}

void testStaleHandleCannotRemoveNewSubscription() {
	Accord accord;
	expect(accord.init(testConfig(1)).ok, "stale handle first init");
	AccordSubscription oldHandle = accord.onRequest([](AccordRequest &request) {
		request.allow();
	});
	expect(static_cast<bool>(oldHandle), "old handle created");
	expect(accord.deinit().ok, "stale handle deinit");
	expect(accord.init(testConfig(1)).ok, "stale handle second init");

	bool newCalled = false;
	AccordSubscription newHandle = accord.onRequest([&newCalled](AccordRequest &request) {
		newCalled = true;
		request.allow();
	});
	expect(static_cast<bool>(newHandle), "new handle created");
	AccordResult staleResult = oldHandle.unsubscribe();
	expect(
	    !staleResult && staleResult.error == AccordError::SubscriptionNotFound,
	    "stale handle is rejected"
	);
	expect(accord.request("new-generation").ok, "new generation request succeeds");
	expect(newCalled, "stale handle did not remove new subscription");
}

void testTimeoutDuringCallback() {
	accordTestMillis = 0;
	AccordConfig config = testConfig();
	config.defaultTimeoutMs = 20;
	Accord accord;
	expect(accord.init(config).ok, "callback timeout init");
	int failedCalls = 0;
	accord.onFailed([&failedCalls](const AccordRequestInfo &, AccordError error) {
		if (error == AccordError::RequestTimeout) {
			failedCalls++;
		}
	});
	accord.onRequest([](AccordRequest &request) {
		accordTestMillis = 25;
		request.allow();
	});

	AccordResult result = accord.request("callback-timeout");
	expect(!result && result.error == AccordError::RequestTimeout, "callback overrun times out");
	expect(accord.getLastFinishState() == AccordState::Failed, "callback timeout state failed");
	expect(accord.getLastError() == AccordError::RequestTimeout, "callback timeout error retained");
	expect(failedCalls == 1, "callback timeout emits failure exactly once");
}

void testTerminalStatePersistsThroughLifecycleCallback() {
	accordTestMillis = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "terminal ordering init");
	accord.onRequest([](AccordRequest &request) {
		request.allow();
	});
	bool sawReady = false;
	bool nestedBlocked = false;
	accord.onReady([&](const AccordRequestInfo &) {
		sawReady = accord.getState() == AccordState::Ready;
		AccordResult nested = accord.request("nested");
		nestedBlocked = !nested && nested.error == AccordError::RequestAlreadyActive;
	});

	expect(accord.request("outer").ok, "terminal ordering request");
	expect(sawReady, "ready state visible during onReady");
	expect(nestedBlocked, "new request blocked during terminal callback");
	expect(accord.getState() == AccordState::Idle, "state returns idle after terminal callback");
}

void testDeferAcrossMillisWraparound() {
	AccordConfig config = testConfig();
	config.minDeferMs = 20;
	config.defaultDeferMs = 20;
	config.maxDeferMs = 20;
	accordTestMillis = 0xfffffff5u;
	Accord accord;
	expect(accord.init(config).ok, "wraparound init");
	int votes = 0;
	accord.onRequest([&votes](AccordRequest &request) {
		votes++;
		if (votes == 1) {
			request.defer(20);
		} else {
			request.allow();
		}
	});
	expect(accord.request("wrap").ok, "wraparound defer accepted");
	accordTestMillis = 8;
	accord.loop();
	expect(votes == 1, "wraparound defer not due early");
	accordTestMillis = 9;
	accord.loop();
	expect(votes == 2, "wraparound defer becomes due using elapsed time");
	expect(accord.getLastFinishState() == AccordState::Ready, "wraparound request finishes ready");
}

void testExternalDeinitWaitsForSubscriberCallback() {
	accordTestMillis = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "external deinit init");

	std::mutex mutex;
	std::condition_variable condition;
	bool entered = false;
	bool release = false;
	std::atomic<bool> deinitFinished{false};
	accord.onRequest([&](AccordRequest &request) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			entered = true;
		}
		condition.notify_all();
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [&release] {
				return release;
			});
		}
		request.allow();
	});

	AccordResult requestResult;
	std::thread requestThread([&] {
		requestResult = accord.request("external-deinit");
	});
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait(lock, [&entered] {
			return entered;
		});
	}
	std::thread deinitThread([&] {
		accord.deinit();
		deinitFinished = true;
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	expect(!deinitFinished.load(), "external deinit waits while callback is running");
	{
		std::lock_guard<std::mutex> lock(mutex);
		release = true;
	}
	condition.notify_all();
	requestThread.join();
	deinitThread.join();
	expect(deinitFinished.load(), "external deinit finishes after callback exits");
	expect(!accord.isInitialized(), "external deinit clears initialized state");
	expect(
	    !requestResult && requestResult.error == AccordError::Cancelled,
	    "external deinit invalidates in-flight vote"
	);
}

void testExternalUnsubscribeWaitsForSubscriberCallback() {
	accordTestMillis = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "external unsubscribe init");

	std::mutex mutex;
	std::condition_variable condition;
	bool entered = false;
	bool release = false;
	std::atomic<bool> unsubscribeFinished{false};
	AccordSubscription subscription = accord.onRequest([&](AccordRequest &request) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			entered = true;
		}
		condition.notify_all();
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [&release] {
				return release;
			});
		}
		request.allow();
	});

	AccordResult requestResult;
	std::thread requestThread([&] {
		requestResult = accord.request("external-unsubscribe");
	});
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait(lock, [&entered] {
			return entered;
		});
	}
	std::thread unsubscribeThread([&] {
		subscription.unsubscribe();
		unsubscribeFinished = true;
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	expect(!unsubscribeFinished.load(), "external unsubscribe waits for in-flight callback");
	{
		std::lock_guard<std::mutex> lock(mutex);
		release = true;
	}
	condition.notify_all();
	requestThread.join();
	unsubscribeThread.join();
	expect(unsubscribeFinished.load(), "external unsubscribe finishes after callback exits");
	expect(requestResult.ok, "request survives removal of in-flight subscriber");
	expect(accord.getLastFinishState() == AccordState::Ready, "unsubscribed in-flight vote is ignored");
}

void testExternalCancelInvalidatesBeforeCallbackExit() {
	accordTestMillis = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "external cancel init");

	std::mutex mutex;
	std::condition_variable condition;
	bool entered = false;
	bool release = false;
	bool secondCalled = false;
	std::atomic<int> cancelledCalls{0};
	accord.onCancelled([&](const AccordRequestInfo &) {
		cancelledCalls++;
	});
	accord.onRequest([&](AccordRequest &request) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			entered = true;
		}
		condition.notify_all();
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [&release] {
				return release;
			});
		}
		request.allow();
	});
	accord.onRequest([&](AccordRequest &request) {
		secondCalled = true;
		request.allow();
	});

	AccordResult requestResult;
	std::thread requestThread([&] {
		requestResult = accord.request("external-cancel");
	});
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait(lock, [&entered] {
			return entered;
		});
	}
	std::thread cancelThread([&] {
		accord.cancel();
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	expect(accord.getState() == AccordState::Cancelled, "external cancel invalidates immediately");
	{
		std::lock_guard<std::mutex> lock(mutex);
		release = true;
	}
	condition.notify_all();
	requestThread.join();
	cancelThread.join();
	expect(
	    !requestResult && requestResult.error == AccordError::Cancelled,
	    "external cancel discards in-flight vote"
	);
	expect(!secondCalled, "external cancel prevents later subscriber admission");
	expect(cancelledCalls.load() == 1, "external cancel callback emitted exactly once");
}

void testConcurrentDeinitDeliversCancellationOnce() {
	accordTestMillis = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "concurrent deinit init");

	std::mutex mutex;
	std::condition_variable condition;
	bool entered = false;
	bool release = false;
	std::atomic<int> cancelledCalls{0};
	accord.onCancelled([&](const AccordRequestInfo &) {
		cancelledCalls++;
	});
	accord.onRequest([&](AccordRequest &request) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			entered = true;
		}
		condition.notify_all();
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [&release] {
				return release;
			});
		}
		request.allow();
	});

	std::thread requestThread([&] {
		accord.request("concurrent-deinit");
	});
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait(lock, [&entered] {
			return entered;
		});
	}
	std::thread first([&] {
		accord.deinit();
	});
	std::thread second([&] {
		accord.deinit();
	});
	for (int attempt = 0; attempt < 1000 && accord.getState() != AccordState::Cancelled; ++attempt) {
		std::this_thread::yield();
	}
	expect(accord.getState() == AccordState::Cancelled, "concurrent deinit invalidates request");
	{
		std::lock_guard<std::mutex> lock(mutex);
		release = true;
	}
	condition.notify_all();
	requestThread.join();
	first.join();
	second.join();
	expect(!accord.isInitialized(), "concurrent deinit completes");
	expect(cancelledCalls.load() == 1, "concurrent deinit emits one cancellation callback");
}

void testDeinitFromLifecycleCallbackDoesNotDeadlock() {
	accordTestMillis = 0;
	Accord accord;
	expect(accord.init(testConfig()).ok, "lifecycle deinit init");
	accord.onRequest([](AccordRequest &request) {
		request.allow();
	});
	bool callbackReturned = false;
	accord.onReady([&](const AccordRequestInfo &) {
		AccordResult result = accord.deinit();
		callbackReturned = result.ok;
	});

	AccordResult requestResult = accord.request("lifecycle-deinit");
	expect(requestResult.ok, "lifecycle deinit request returns accepted");
	expect(callbackReturned, "deinit from lifecycle callback returns");
	expect(!accord.isInitialized(), "lifecycle self deinit completes after callback");
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
	testStaleHandleCannotRemoveNewSubscription();
	testTimeoutDuringCallback();
	testTerminalStatePersistsThroughLifecycleCallback();
	testDeferAcrossMillisWraparound();
	testExternalCancelInvalidatesBeforeCallbackExit();
	testConcurrentDeinitDeliversCancellationOnce();
	testExternalDeinitWaitsForSubscriberCallback();
	testExternalUnsubscribeWaitsForSubscriberCallback();
	testDeinitFromLifecycleCallbackDoesNotDeadlock();

	if (failureCount != 0) {
		std::printf("%d host tests failed\n", failureCount);
		return 1;
	}

	std::printf("host tests passed\n");
	return 0;
}
