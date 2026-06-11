#include "Accord.h"

#include "internal/AccordMutex.h"

#include <algorithm>
#include <new>
#include <utility>

namespace {
constexpr AccordSubscriptionId kInvalidSubscriptionId = 0;

bool isActiveState(AccordState state) {
	return state == AccordState::CollectingVotes || state == AccordState::Deferred;
}

bool isEmpty(const char *value) {
	return value == nullptr || value[0] == '\0';
}

bool timeoutElapsed(uint32_t startedMs, uint32_t timeoutMs) {
	if (timeoutMs == 0) {
		return false;
	}
	return millis() - startedMs >= timeoutMs;
}

uint32_t nextNonZero(uint32_t value) {
	value++;
	if (value == 0) {
		value++;
	}
	return value;
}

uint32_t clampDeferMs(uint32_t requestedMs, const AccordConfig &config) {
	uint32_t value = requestedMs == 0 ? config.defaultDeferMs : requestedMs;
	value = std::max(value, config.minDeferMs);
	value = std::min(value, config.maxDeferMs);
	return value;
}
} // namespace

struct AccordSubscriptionRecord {
	AccordSubscriptionId id = kInvalidSubscriptionId;
	bool active = false;
	AccordRequestCallback callback;

	bool available() const {
		return !active;
	}

	void clear() {
		id = kInvalidSubscriptionId;
		active = false;
		callback = nullptr;
	}
};

struct AccordSubscriberSnapshot {
	AccordSubscriptionId id = kInvalidSubscriptionId;
	AccordRequestCallback callback;
};

enum class AccordFinishKind : uint8_t {
	None,
	Ready,
	Rejected,
	Deferred,
	Failed,
	Cancelled,
};

struct AccordFinishEvent {
	AccordFinishKind kind = AccordFinishKind::None;
	AccordRequestInfo info{};
	AccordError error = AccordError::None;
	uint32_t retryAfterMs = 0;
	AccordReadyCallback readyCallback;
	AccordRejectedCallback rejectedCallback;
	AccordDeferredCallback deferredCallback;
	AccordFailedCallback failedCallback;
	AccordCancelledCallback cancelledCallback;
};

struct AccordImpl {
	AccordConfig config{};
	AccordMutex mutex;
	std::unique_ptr<AccordSubscriptionRecord[]> subscribers;
	size_t subscriberCapacity = 0;
	size_t activeSubscriberCount = 0;
	AccordReadyCallback readyCallback;
	AccordRejectedCallback rejectedCallback;
	AccordDeferredCallback deferredCallback;
	AccordFailedCallback failedCallback;
	AccordCancelledCallback cancelledCallback;
	bool initialized = false;
	AccordState state = AccordState::Idle;
	AccordRequestInfo info{};
	bool lastInfoValid = false;
	AccordRequestInfo lastInfo{};
	AccordState lastFinishState = AccordState::Idle;
	AccordError lastError = AccordError::None;
	uint32_t requestId = 0;
	AccordSubscriptionId nextSubscriptionId = 1;
	uint32_t retryDueAtMs = 0;
	uint32_t retryAfterMs = 0;

	void clearRequestLocked() {
		info = AccordRequestInfo();
		retryDueAtMs = 0;
		retryAfterMs = 0;
		state = AccordState::Idle;
	}

	void clearSubscribersLocked() {
		if (!subscribers) {
			activeSubscriberCount = 0;
			return;
		}
		for (size_t i = 0; i < subscriberCapacity; ++i) {
			subscribers[i].clear();
		}
		activeSubscriberCount = 0;
	}

	AccordSubscriptionRecord *findSubscriberLocked(AccordSubscriptionId id) {
		if (id == kInvalidSubscriptionId || !subscribers) {
			return nullptr;
		}
		for (size_t i = 0; i < subscriberCapacity; ++i) {
			if (subscribers[i].active && subscribers[i].id == id) {
				return &subscribers[i];
			}
		}
		return nullptr;
	}

	bool allocateSubscriptionIdLocked(AccordSubscriptionId &out) {
		for (uint32_t attempts = 0; attempts < UINT32_MAX; ++attempts) {
			const AccordSubscriptionId id = nextSubscriptionId++;
			if (id == kInvalidSubscriptionId) {
				continue;
			}
			if (findSubscriberLocked(id) == nullptr) {
				out = id;
				return true;
			}
		}
		return false;
	}

	AccordFinishEvent makeEventLocked(AccordFinishKind kind, AccordError error = AccordError::None) {
		AccordFinishEvent event;
		event.kind = kind;
		event.info = info;
		event.error = error;
		event.retryAfterMs = retryAfterMs;
		event.readyCallback = readyCallback;
		event.rejectedCallback = rejectedCallback;
		event.deferredCallback = deferredCallback;
		event.failedCallback = failedCallback;
		event.cancelledCallback = cancelledCallback;
		return event;
	}
};

AccordResult AccordResult::success(const char *message) {
	AccordResult result;
	result.ok = true;
	result.error = AccordError::None;
	result.message = message;
	return result;
}

AccordResult AccordResult::failure(AccordError error, const char *message) {
	AccordResult result;
	result.ok = false;
	result.error = error;
	result.message = message;
	return result;
}

AccordSubscriptionResult AccordSubscriptionResult::success(AccordSubscription subscription) {
	AccordSubscriptionResult result;
	result.ok = true;
	result.error = AccordError::None;
	result.message = "ok";
	result.subscription = std::move(subscription);
	return result;
}

AccordSubscriptionResult AccordSubscriptionResult::failure(
    AccordError error,
    const char *message
) {
	AccordSubscriptionResult result;
	result.ok = false;
	result.error = error;
	result.message = message;
	return result;
}

AccordRequest::AccordRequest(const char *label, uint32_t retryCount, uint32_t requestId)
    : _label(label), _retryCount(retryCount), _requestId(requestId) {
}

void AccordRequest::allow() {
	setDecision(AccordDecision::Allow, nullptr, 0);
}

void AccordRequest::reject() {
	reject(nullptr);
}

void AccordRequest::reject(const char *message) {
	setDecision(AccordDecision::Reject, message, 0);
}

void AccordRequest::defer(uint32_t retryAfterMs) {
	setDecision(AccordDecision::Defer, nullptr, retryAfterMs);
}

const char *AccordRequest::label() const {
	return _label;
}

uint32_t AccordRequest::retryCount() const {
	return _retryCount;
}

bool AccordRequest::hasDecision() const {
	return _decision != AccordDecision::None;
}

AccordDecision AccordRequest::decision() const {
	return _decision;
}

const char *AccordRequest::rejectMessage() const {
	return _rejectMessage;
}

uint32_t AccordRequest::retryAfterMs() const {
	return _retryAfterMs;
}

uint32_t AccordRequest::requestId() const {
	return _requestId;
}

void AccordRequest::setDecision(
    AccordDecision decision,
    const char *message,
    uint32_t retryAfterMs
) {
	if (_decision != AccordDecision::None) {
		return;
	}
	_decision = decision;
	_rejectMessage = message;
	_retryAfterMs = retryAfterMs;
}

AccordSubscription::AccordSubscription(Accord *accord, AccordSubscriptionId id)
    : _accord(accord), _id(id) {
}

AccordSubscription::~AccordSubscription() = default;

AccordSubscription::AccordSubscription(AccordSubscription &&other) noexcept {
	_accord = other._accord;
	_id = other._id;
	other._accord = nullptr;
	other._id = kInvalidSubscriptionId;
}

AccordSubscription &AccordSubscription::operator=(AccordSubscription &&other) noexcept {
	if (this == &other) {
		return *this;
	}
	_accord = other._accord;
	_id = other._id;
	other._accord = nullptr;
	other._id = kInvalidSubscriptionId;
	return *this;
}

AccordResult AccordSubscription::unsubscribe() {
	if (_accord == nullptr || _id == kInvalidSubscriptionId) {
		return AccordResult::success("accord subscription handle is empty");
	}
	Accord *accord = _accord;
	const AccordSubscriptionId id = _id;
	_accord = nullptr;
	_id = kInvalidSubscriptionId;
	return accord->unsubscribe(id);
}

AccordSubscriptionId AccordSubscription::release() {
	const AccordSubscriptionId id = _id;
	_accord = nullptr;
	_id = kInvalidSubscriptionId;
	return id;
}

static void invokeEvent(const AccordFinishEvent &event) {
	if (event.kind == AccordFinishKind::Ready && event.readyCallback) {
		event.readyCallback(event.info);
		return;
	}
	if (event.kind == AccordFinishKind::Rejected && event.rejectedCallback) {
		event.rejectedCallback(event.info);
		return;
	}
	if (event.kind == AccordFinishKind::Deferred && event.deferredCallback) {
		event.deferredCallback(event.info, event.retryAfterMs);
		return;
	}
	if (event.kind == AccordFinishKind::Failed && event.failedCallback) {
		event.failedCallback(event.info, event.error);
		return;
	}
	if (event.kind == AccordFinishKind::Cancelled && event.cancelledCallback) {
		event.cancelledCallback(event.info);
	}
}

static void finishRequestLocked(AccordImpl &impl, AccordFinishEvent &event) {
	const AccordFinishKind kind = event.kind;
	if (kind == AccordFinishKind::Ready) {
		impl.state = AccordState::Ready;
	} else if (kind == AccordFinishKind::Rejected) {
		impl.state = AccordState::Rejected;
	} else if (kind == AccordFinishKind::Failed) {
		impl.state = AccordState::Failed;
	} else if (kind == AccordFinishKind::Cancelled) {
		impl.state = AccordState::Cancelled;
	}
	impl.lastInfo = impl.info;
	impl.lastInfoValid = true;
	impl.lastFinishState = impl.state;
	impl.lastError = event.error;
	event = impl.makeEventLocked(kind, event.error);
	impl.requestId = nextNonZero(impl.requestId);
	impl.clearRequestLocked();
}

Accord::Accord() : _impl(new (std::nothrow) AccordImpl()) {
}

Accord::~Accord() {
	deinit();
}

AccordResult Accord::init(const AccordConfig &config) {
	if (!_impl) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord allocation failed");
	}
	if (config.maxSubscribers == 0 || config.maxSubscribers > UINT16_MAX ||
	    config.minDeferMs == 0 || config.defaultDeferMs == 0 || config.maxDeferMs == 0 ||
	    config.minDeferMs > config.defaultDeferMs || config.defaultDeferMs > config.maxDeferMs) {
		return AccordResult::failure(AccordError::InvalidConfig, "accord config is invalid");
	}

	AccordLock lock(_impl->mutex);
	if (!lock) {
		return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
	}
	if (_impl->initialized) {
		return AccordResult::failure(AccordError::AlreadyInitialized, "accord is already initialized");
	}

	std::unique_ptr<AccordSubscriptionRecord[]> subscribers(
	    new (std::nothrow) AccordSubscriptionRecord[config.maxSubscribers]
	);
	if (!subscribers) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord subscriber allocation failed");
	}

	_impl->config = config;
	_impl->subscribers = std::move(subscribers);
	_impl->subscriberCapacity = config.maxSubscribers;
	_impl->activeSubscriberCount = 0;
	_impl->initialized = true;
	_impl->state = AccordState::Idle;
	_impl->info = AccordRequestInfo();
	_impl->lastInfoValid = false;
	_impl->lastInfo = AccordRequestInfo();
	_impl->lastFinishState = AccordState::Idle;
	_impl->lastError = AccordError::None;
	_impl->retryDueAtMs = 0;
	_impl->retryAfterMs = 0;
	_impl->requestId = nextNonZero(_impl->requestId);
	_impl->nextSubscriptionId = 1;
	return AccordResult::success("accord initialized");
}

AccordResult Accord::deinit() {
	if (!_impl) {
		return AccordResult::success("accord deinitialized");
	}

	AccordFinishEvent event;
	{
		AccordLock lock(_impl->mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		if (!_impl->initialized) {
			return AccordResult::success("accord is not initialized");
		}
		if (isActiveState(_impl->state)) {
			event.kind = AccordFinishKind::Cancelled;
			event.error = AccordError::Cancelled;
			finishRequestLocked(*_impl, event);
		} else {
			_impl->requestId = nextNonZero(_impl->requestId);
			_impl->clearRequestLocked();
		}
		_impl->clearSubscribersLocked();
		_impl->readyCallback = nullptr;
		_impl->rejectedCallback = nullptr;
		_impl->deferredCallback = nullptr;
		_impl->failedCallback = nullptr;
		_impl->cancelledCallback = nullptr;
		_impl->subscribers.reset();
		_impl->subscriberCapacity = 0;
		_impl->initialized = false;
	}
	invokeEvent(event);
	return AccordResult::success("accord deinitialized");
}

AccordResult Accord::request(const char *label) {
	if (isEmpty(label)) {
		return AccordResult::failure(AccordError::InvalidArgument, "request label is required");
	}
	if (!_impl) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord allocation failed");
	}

	AccordFinishEvent event;
	{
		AccordLock lock(_impl->mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		if (!_impl->initialized) {
			return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
		}
		if (isActiveState(_impl->state)) {
			return AccordResult::failure(
			    AccordError::RequestAlreadyActive,
			    "accord request already active"
			);
		}

		_impl->requestId = nextNonZero(_impl->requestId);
		_impl->state = AccordState::CollectingVotes;
		_impl->info = AccordRequestInfo();
		_impl->info.label = label;
		_impl->info.startedAtMs = millis();
		_impl->info.subscriberCount = _impl->activeSubscriberCount;
		_impl->retryDueAtMs = 0;
		_impl->retryAfterMs = 0;

		if (_impl->activeSubscriberCount == 0) {
			event.kind = _impl->config.allowWithoutSubscribers ? AccordFinishKind::Ready
			                                                   : AccordFinishKind::Failed;
			event.error = _impl->config.allowWithoutSubscribers ? AccordError::None
			                                                    : AccordError::NoSubscribers;
			finishRequestLocked(*_impl, event);
		}
	}

	if (event.kind != AccordFinishKind::None) {
		invokeEvent(event);
		return event.error == AccordError::None
		           ? AccordResult::success("accord request ready")
		           : AccordResult::failure(event.error, "accord request failed");
	}
	return processVotes();
}

AccordResult Accord::cancel() {
	if (!_impl) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord allocation failed");
	}

	AccordFinishEvent event;
	{
		AccordLock lock(_impl->mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		if (!_impl->initialized) {
			return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
		}
		if (!isActiveState(_impl->state)) {
			return AccordResult::success("accord request is not active");
		}
		event.kind = AccordFinishKind::Cancelled;
		event.error = AccordError::Cancelled;
		finishRequestLocked(*_impl, event);
	}
	invokeEvent(event);
	return AccordResult::success("accord request cancelled");
}

AccordResult Accord::force(const char *label) {
	if (isEmpty(label)) {
		return AccordResult::failure(AccordError::InvalidArgument, "force label is required");
	}
	if (!_impl) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord allocation failed");
	}

	AccordFinishEvent event;
	{
		AccordLock lock(_impl->mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		if (!_impl->initialized) {
			return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
		}
		if (isActiveState(_impl->state)) {
			return AccordResult::failure(
			    AccordError::RequestAlreadyActive,
			    "accord force request cannot interrupt active request"
			);
		}

		_impl->requestId = nextNonZero(_impl->requestId);
		_impl->state = AccordState::Ready;
		_impl->info = AccordRequestInfo();
		_impl->info.label = label;
		_impl->info.startedAtMs = millis();
		_impl->info.subscriberCount = _impl->activeSubscriberCount;
		event.kind = AccordFinishKind::Ready;
		finishRequestLocked(*_impl, event);
	}
	invokeEvent(event);
	return AccordResult::success("accord force request ready");
}

AccordSubscriptionResult Accord::subscribe(AccordRequestCallback callback) {
	if (!_impl || !callback) {
		return AccordSubscriptionResult::failure(
		    AccordError::InvalidArgument,
		    "accord request callback is required"
		);
	}

	AccordLock lock(_impl->mutex);
	if (!lock) {
		return AccordSubscriptionResult::failure(
		    AccordError::InternalError,
		    "accord mutex unavailable"
		);
	}
	if (!_impl->initialized || !_impl->subscribers) {
		return AccordSubscriptionResult::failure(
		    AccordError::NotInitialized,
		    "accord is not initialized"
		);
	}
	if (_impl->activeSubscriberCount >= _impl->subscriberCapacity) {
		return AccordSubscriptionResult::failure(
		    AccordError::SubscriberLimitReached,
		    "accord subscriber limit reached"
		);
	}

	AccordSubscriptionRecord *slot = nullptr;
	for (size_t i = 0; i < _impl->subscriberCapacity; ++i) {
		if (_impl->subscribers[i].available()) {
			slot = &_impl->subscribers[i];
			break;
		}
	}
	if (slot == nullptr) {
		return AccordSubscriptionResult::failure(
		    AccordError::SubscriberLimitReached,
		    "accord subscriber limit reached"
		);
	}

	AccordSubscriptionId id = kInvalidSubscriptionId;
	if (!_impl->allocateSubscriptionIdLocked(id)) {
		return AccordSubscriptionResult::failure(
		    AccordError::InternalError,
		    "accord subscription id allocation failed"
		);
	}

	slot->id = id;
	slot->active = true;
	slot->callback = callback;
	_impl->activeSubscriberCount++;
	return AccordSubscriptionResult::success(AccordSubscription(this, id));
}

AccordSubscription Accord::onRequest(AccordRequestCallback callback) {
	AccordSubscriptionResult result = subscribe(callback);
	if (!result) {
		return AccordSubscription();
	}
	return std::move(result.subscription);
}

void Accord::onReady(AccordReadyCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->readyCallback = callback;
	}
}

void Accord::onRejected(AccordRejectedCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->rejectedCallback = callback;
	}
}

void Accord::onDeferred(AccordDeferredCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->deferredCallback = callback;
	}
}

void Accord::onFailed(AccordFailedCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->failedCallback = callback;
	}
}

void Accord::onCancelled(AccordCancelledCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->cancelledCallback = callback;
	}
}

void Accord::loop() {
	if (!_impl) {
		return;
	}

	AccordFinishEvent event;
	bool shouldRetry = false;
	{
		AccordLock lock(_impl->mutex);
		if (!lock || !_impl->initialized || !isActiveState(_impl->state)) {
			return;
		}
		if (timeoutElapsed(_impl->info.startedAtMs, _impl->config.defaultTimeoutMs)) {
			event.kind = AccordFinishKind::Failed;
			event.error = AccordError::RequestTimeout;
			finishRequestLocked(*_impl, event);
		} else if (_impl->state == AccordState::Deferred &&
		           static_cast<int32_t>(millis() - _impl->retryDueAtMs) >= 0) {
			shouldRetry = true;
			_impl->state = AccordState::CollectingVotes;
		}
	}
	if (event.kind != AccordFinishKind::None) {
		invokeEvent(event);
		return;
	}
	if (shouldRetry) {
		processVotes();
	}
}

bool Accord::isInitialized() const {
	if (!_impl) {
		return false;
	}
	AccordLock lock(_impl->mutex);
	return lock && _impl->initialized;
}

bool Accord::isRequestActive() const {
	if (!_impl) {
		return false;
	}
	AccordLock lock(_impl->mutex);
	return lock && _impl->initialized && isActiveState(_impl->state);
}

AccordState Accord::getState() const {
	if (!_impl) {
		return AccordState::Idle;
	}
	AccordLock lock(_impl->mutex);
	if (!lock) {
		return AccordState::Idle;
	}
	return _impl->state;
}

bool Accord::getRequestInfo(AccordRequestInfo &info) const {
	if (!_impl) {
		return false;
	}
	AccordLock lock(_impl->mutex);
	if (!lock || !_impl->initialized || !isActiveState(_impl->state)) {
		return false;
	}
	info = _impl->info;
	return true;
}

bool Accord::getLastRequestInfo(AccordRequestInfo &info) const {
	if (!_impl) {
		return false;
	}
	AccordLock lock(_impl->mutex);
	if (!lock || !_impl->lastInfoValid) {
		return false;
	}
	info = _impl->lastInfo;
	return true;
}

AccordError Accord::getLastError() const {
	if (!_impl) {
		return AccordError::InternalError;
	}
	AccordLock lock(_impl->mutex);
	if (!lock || !_impl->lastInfoValid) {
		return AccordError::None;
	}
	return _impl->lastError;
}

AccordState Accord::getLastFinishState() const {
	if (!_impl) {
		return AccordState::Idle;
	}
	AccordLock lock(_impl->mutex);
	if (!lock || !_impl->lastInfoValid) {
		return AccordState::Idle;
	}
	return _impl->lastFinishState;
}

const char *Accord::errorToString(AccordError error) const {
	switch (error) {
		case AccordError::None:
			return "none";
		case AccordError::NotInitialized:
			return "not initialized";
		case AccordError::AlreadyInitialized:
			return "already initialized";
		case AccordError::RequestAlreadyActive:
			return "request already active";
		case AccordError::NoSubscribers:
			return "no subscribers";
		case AccordError::SubscriberLimitReached:
			return "subscriber limit reached";
		case AccordError::RequestTimeout:
			return "request timeout";
		case AccordError::MaxRetriesReached:
			return "max retries reached";
		case AccordError::MissingVote:
			return "missing vote";
		case AccordError::InvalidConfig:
			return "invalid config";
		case AccordError::InvalidArgument:
			return "invalid argument";
		case AccordError::OutOfMemory:
			return "out of memory";
		case AccordError::Cancelled:
			return "cancelled";
		case AccordError::SubscriptionNotFound:
			return "subscription not found";
		case AccordError::InternalError:
			return "internal error";
	}
	return "unknown";
}

const char *Accord::stateToString(AccordState state) const {
	switch (state) {
		case AccordState::Idle:
			return "idle";
		case AccordState::CollectingVotes:
			return "collecting votes";
		case AccordState::Deferred:
			return "deferred";
		case AccordState::Ready:
			return "ready";
		case AccordState::Rejected:
			return "rejected";
		case AccordState::Failed:
			return "failed";
		case AccordState::Cancelled:
			return "cancelled";
	}
	return "unknown";
}

AccordResult Accord::unsubscribe(AccordSubscriptionId subscriptionId) {
	if (subscriptionId == kInvalidSubscriptionId) {
		return AccordResult::failure(AccordError::InvalidArgument, "subscription id is required");
	}
	if (!_impl) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord allocation failed");
	}

	AccordLock lock(_impl->mutex);
	if (!lock) {
		return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
	}
	if (!_impl->initialized) {
		return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
	}

	AccordSubscriptionRecord *record = _impl->findSubscriberLocked(subscriptionId);
	if (record == nullptr) {
		return AccordResult::failure(AccordError::SubscriptionNotFound, "subscription not found");
	}
	record->clear();
	if (_impl->activeSubscriberCount > 0) {
		_impl->activeSubscriberCount--;
	}
	return AccordResult::success("accord subscription removed");
}

AccordResult Accord::processVotes() {
	if (!_impl) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord allocation failed");
	}
	AccordImpl &impl = *_impl;
	std::unique_ptr<AccordSubscriberSnapshot[]> snapshots;
	size_t snapshotCount = 0;
	const char *label = nullptr;
	uint32_t retryCount = 0;
	uint32_t requestId = 0;
	AccordFinishEvent earlyEvent;
	AccordResult earlyResult = AccordResult::success();

	{
		AccordLock lock(impl.mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		if (!impl.initialized) {
			return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
		}
		if (!isActiveState(impl.state)) {
			return AccordResult::failure(AccordError::InvalidArgument, "accord request is not active");
		}
		if (timeoutElapsed(impl.info.startedAtMs, impl.config.defaultTimeoutMs)) {
			earlyEvent.kind = AccordFinishKind::Failed;
			earlyEvent.error = AccordError::RequestTimeout;
			finishRequestLocked(impl, earlyEvent);
			earlyResult =
			    AccordResult::failure(AccordError::RequestTimeout, "accord request timed out");
		}

		if (earlyEvent.kind == AccordFinishKind::None) {
			snapshotCount = impl.activeSubscriberCount;
		}
		if (earlyEvent.kind == AccordFinishKind::None && snapshotCount == 0) {
			earlyEvent.kind = impl.config.allowWithoutSubscribers ? AccordFinishKind::Ready
			                                                       : AccordFinishKind::Failed;
			earlyEvent.error =
			    impl.config.allowWithoutSubscribers ? AccordError::None : AccordError::NoSubscribers;
			finishRequestLocked(impl, earlyEvent);
			earlyResult =
			    earlyEvent.error == AccordError::None
			        ? AccordResult::success("accord request ready")
			        : AccordResult::failure(earlyEvent.error, "accord request failed");
		}

		if (earlyEvent.kind == AccordFinishKind::None) {
			snapshots.reset(new (std::nothrow) AccordSubscriberSnapshot[snapshotCount]);
			if (!snapshots) {
				earlyEvent.kind = AccordFinishKind::Failed;
				earlyEvent.error = AccordError::OutOfMemory;
				finishRequestLocked(impl, earlyEvent);
				earlyResult =
				    AccordResult::failure(AccordError::OutOfMemory, "accord snapshot allocation failed");
			}
		}

		if (earlyEvent.kind == AccordFinishKind::None) {
			size_t writeIndex = 0;
			for (size_t i = 0; i < impl.subscriberCapacity && writeIndex < snapshotCount; ++i) {
				const AccordSubscriptionRecord &record = impl.subscribers[i];
				if (record.active && record.callback) {
					snapshots[writeIndex].id = record.id;
					snapshots[writeIndex].callback = record.callback;
					writeIndex++;
				}
			}
			snapshotCount = writeIndex;
			label = impl.info.label;
			retryCount = impl.info.retryCount;
			requestId = impl.requestId;
			impl.info.subscriberCount = snapshotCount;
			impl.info.allowCount = 0;
			impl.info.rejectCount = 0;
			impl.info.deferCount = 0;
			impl.info.rejectMessage = nullptr;
			impl.retryAfterMs = 0;
		}
	}

	if (earlyEvent.kind != AccordFinishKind::None) {
		invokeEvent(earlyEvent);
		return earlyResult;
	}

	for (size_t i = 0; i < snapshotCount; ++i) {
		{
			AccordLock lock(impl.mutex);
			if (!lock) {
				return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
			}
			if (!impl.initialized || impl.requestId != requestId || !isActiveState(impl.state)) {
				return AccordResult::failure(
				    AccordError::Cancelled,
				    "accord request is no longer active"
				);
			}
			if (impl.findSubscriberLocked(snapshots[i].id) == nullptr) {
				continue;
			}
		}

		AccordRequest request(label, retryCount, requestId);
		snapshots[i].callback(request);

		AccordFinishEvent event;
		{
			AccordLock lock(impl.mutex);
			if (!lock) {
				return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
			}
			if (!impl.initialized || impl.requestId != requestId || !isActiveState(impl.state)) {
				return AccordResult::failure(
				    AccordError::Cancelled,
				    "accord request is no longer active"
				);
			}

			if (impl.findSubscriberLocked(snapshots[i].id) == nullptr) {
				continue;
			}
			if (!request.hasDecision()) {
				event.kind = AccordFinishKind::Failed;
				event.error = AccordError::MissingVote;
				finishRequestLocked(impl, event);
			} else if (request.decision() == AccordDecision::Allow) {
				impl.info.allowCount++;
			} else if (request.decision() == AccordDecision::Reject) {
				impl.info.rejectCount++;
				if (impl.info.rejectMessage == nullptr && request.rejectMessage() != nullptr) {
					impl.info.rejectMessage = request.rejectMessage();
				}
			} else if (request.decision() == AccordDecision::Defer) {
				impl.info.deferCount++;
				impl.retryAfterMs =
				    std::max(impl.retryAfterMs, clampDeferMs(request.retryAfterMs(), impl.config));
			}
		}
		if (event.kind != AccordFinishKind::None) {
			invokeEvent(event);
			return AccordResult::failure(event.error, "accord request failed");
		}
	}

	AccordFinishEvent event;
	AccordError error = AccordError::None;
	{
		AccordLock lock(impl.mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		if (!impl.initialized || impl.requestId != requestId || !isActiveState(impl.state)) {
			return AccordResult::failure(AccordError::Cancelled, "accord request is no longer active");
		}

		if (impl.info.rejectCount > 0) {
			event.kind = AccordFinishKind::Rejected;
			finishRequestLocked(impl, event);
		} else if (impl.info.deferCount > 0) {
			if (impl.info.retryCount >= impl.config.maxRetries) {
				event.kind = AccordFinishKind::Failed;
				event.error = AccordError::MaxRetriesReached;
				error = AccordError::MaxRetriesReached;
				finishRequestLocked(impl, event);
			} else {
				impl.info.retryCount++;
				impl.state = AccordState::Deferred;
				impl.retryDueAtMs = millis() + impl.retryAfterMs;
				event = impl.makeEventLocked(AccordFinishKind::Deferred);
			}
		} else {
			event.kind = AccordFinishKind::Ready;
			finishRequestLocked(impl, event);
		}
	}

	invokeEvent(event);
	if (event.kind == AccordFinishKind::Failed) {
		return AccordResult::failure(error, "accord request failed");
	}
	return AccordResult::success("accord request accepted");
}
