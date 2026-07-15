#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

struct AccordImpl;
struct AccordResult;
class Accord;
class AccordRequest;

using AccordSubscriptionId = uint32_t;

enum class AccordError : uint8_t {
	None,
	NotInitialized,
	AlreadyInitialized,
	RequestAlreadyActive,
	NoSubscribers,
	SubscriberLimitReached,
	RequestTimeout,
	MaxRetriesReached,
	MissingVote,
	InvalidConfig,
	InvalidArgument,
	OutOfMemory,
	Cancelled,
	SubscriptionNotFound,
	InternalError,
};

enum class AccordState : uint8_t {
	Idle,
	CollectingVotes,
	Deferred,
	Ready,
	Rejected,
	Failed,
	Cancelled,
};

enum class AccordDecision : uint8_t {
	None,
	Allow,
	Reject,
	Defer,
};

struct AccordConfig {
	uint32_t defaultTimeoutMs = 30000;
	uint8_t maxRetries = 10;
	uint32_t minDeferMs = 10;
	uint32_t defaultDeferMs = 1000;
	uint32_t maxDeferMs = 60000;
	bool allowWithoutSubscribers = true;
	size_t maxSubscribers = 16;
};

struct AccordResult {
	bool ok = false;
	AccordError error = AccordError::InternalError;
	const char *message = "internal error";

	explicit operator bool() const {
		return ok;
	}

	static AccordResult success(const char *message = "ok");
	static AccordResult failure(AccordError error, const char *message);
};

struct AccordRequestInfo {
	const char *label = nullptr;
	const char *rejectMessage = nullptr;
	uint32_t startedAtMs = 0;
	uint32_t retryCount = 0;
	uint32_t subscriberCount = 0;
	uint32_t allowCount = 0;
	uint32_t rejectCount = 0;
	uint32_t deferCount = 0;
};

class AccordRequest {
  public:
	AccordRequest() = default;

	void allow();
	void reject();
	void reject(const char *message);
	void defer(uint32_t retryAfterMs);

	const char *label() const;
	uint32_t retryCount() const;

	bool hasDecision() const;
	AccordDecision decision() const;
	const char *rejectMessage() const;
	uint32_t retryAfterMs() const;
	uint32_t requestId() const;

  private:
	friend class Accord;

	AccordRequest(const char *label, uint32_t retryCount, uint32_t requestId);

	void setDecision(AccordDecision decision, const char *message, uint32_t retryAfterMs);

	const char *_label = nullptr;
	uint32_t _retryCount = 0;
	uint32_t _requestId = 0;
	AccordDecision _decision = AccordDecision::None;
	const char *_rejectMessage = nullptr;
	uint32_t _retryAfterMs = 0;
};

using AccordRequestCallback = std::function<void(AccordRequest &)>;
using AccordReadyCallback = std::function<void(const AccordRequestInfo &)>;
using AccordRejectedCallback = std::function<void(const AccordRequestInfo &)>;
using AccordDeferredCallback = std::function<void(const AccordRequestInfo &, uint32_t retryAfterMs)>;
using AccordFailedCallback = std::function<void(const AccordRequestInfo &, AccordError)>;
using AccordCancelledCallback = std::function<void(const AccordRequestInfo &)>;

class AccordSubscription {
  public:
	AccordSubscription() = default;
	AccordSubscription(Accord *accord, AccordSubscriptionId id);
	~AccordSubscription();

	AccordSubscription(const AccordSubscription &) = delete;
	AccordSubscription &operator=(const AccordSubscription &) = delete;

	AccordSubscription(AccordSubscription &&other) noexcept;
	AccordSubscription &operator=(AccordSubscription &&other) noexcept;

	explicit operator bool() const {
		return _accord != nullptr && _id != 0;
	}

	AccordSubscriptionId id() const {
		return _id;
	}

	AccordResult unsubscribe();
	AccordSubscriptionId release();

  private:
	friend class Accord;

	AccordSubscription(Accord *accord, AccordSubscriptionId id, uint32_t generation);

	Accord *_accord = nullptr;
	AccordSubscriptionId _id = 0;
	uint32_t _generation = 0;
};

struct AccordSubscriptionResult {
	bool ok = false;
	AccordError error = AccordError::InternalError;
	const char *message = "internal error";
	AccordSubscription subscription;

	explicit operator bool() const {
		return ok;
	}

	static AccordSubscriptionResult success(AccordSubscription subscription);
	static AccordSubscriptionResult failure(AccordError error, const char *message);
};

class Accord {
  public:
	Accord();
	~Accord();

	Accord(const Accord &) = delete;
	Accord &operator=(const Accord &) = delete;

	AccordResult init(const AccordConfig &config = AccordConfig());
	AccordResult deinit();

	AccordResult request(const char *label);
	AccordResult cancel();
	AccordResult force(const char *label);

	AccordSubscriptionResult subscribe(AccordRequestCallback callback);
	AccordSubscription onRequest(AccordRequestCallback callback);

	void onReady(AccordReadyCallback callback);
	void onRejected(AccordRejectedCallback callback);
	void onDeferred(AccordDeferredCallback callback);
	void onFailed(AccordFailedCallback callback);
	void onCancelled(AccordCancelledCallback callback);

	void loop();

	bool isInitialized() const;
	bool isRequestActive() const;
	AccordState getState() const;
	bool getRequestInfo(AccordRequestInfo &info) const;
	bool getLastRequestInfo(AccordRequestInfo &info) const;
	AccordError getLastError() const;
	AccordState getLastFinishState() const;

	const char *errorToString(AccordError error) const;
	const char *stateToString(AccordState state) const;
	AccordResult unsubscribe(AccordSubscriptionId subscriptionId);

  private:
	friend class AccordSubscription;

	AccordResult processVotes();
	AccordResult unsubscribe(AccordSubscriptionId subscriptionId, uint32_t generation);

	std::unique_ptr<AccordImpl> _impl;
};
