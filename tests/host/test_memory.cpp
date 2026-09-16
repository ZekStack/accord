#include <Accord.h>

#include <atomic>
#include <cstdio>

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

void testDefaultMemoryPolicy() {
	Accord accord;
	AccordResult result = accord.init();
	expect(result.ok, "default memory policy initializes");
	expect(accord.deinit().ok, "default memory policy deinitializes");
}

void testPlacementCanChangeAcrossReinit() {
	Accord accord;
	AccordConfig internalConfig;
	internalConfig.memory.allocation = Strata::Placement::Internal;
	expect(accord.init(internalConfig).ok, "internal allocation initializes");
	expect(accord.deinit().ok, "internal allocation deinitializes");

	AccordConfig preferredExternalConfig;
	preferredExternalConfig.memory.allocation = Strata::Placement::PreferExternal;
	expect(accord.init(preferredExternalConfig).ok, "preferred external allocation reinitializes");
	expect(accord.deinit().ok, "preferred external allocation deinitializes");
}

void testUnsupportedRequiredExternalIsTransactional() {
	Accord accord;
	AccordConfig config;
	config.memory.allocation = Strata::Placement::RequireExternal;
	AccordResult result = accord.init(config);
	expect(!result && result.error == AccordError::OutOfMemory, "required external failure reports out of memory");
	expect(!accord.isInitialized(), "failed storage allocation leaves Accord uninitialized");

	AccordConfig fallback;
	fallback.memory.allocation = Strata::Placement::Internal;
	expect(accord.init(fallback).ok, "Accord initializes after transactional allocation failure");
}

void testInvalidMemoryPolicyIsRejected() {
	Accord accord;
	AccordConfig config;
	config.memory.allocation = static_cast<Strata::Placement>(0xff);
	AccordResult result = accord.init(config);
	expect(!result && result.error == AccordError::InvalidConfig, "invalid allocation placement is rejected");

	AccordConfig taskStackConfig;
	taskStackConfig.memory.taskStack = static_cast<Strata::Placement>(0xff);
	result = accord.init(taskStackConfig);
	expect(!result && result.error == AccordError::InvalidConfig, "invalid memory policy is rejected");
}
} // namespace

int main() {
	testDefaultMemoryPolicy();
	testPlacementCanChangeAcrossReinit();
	testUnsupportedRequiredExternalIsTransactional();
	testInvalidMemoryPolicyIsRejected();

	if (failureCount != 0) {
		std::printf("Accord memory tests failed: %d\n", failureCount);
		return 1;
	}
	std::printf("Accord memory tests passed\n");
	return 0;
}
