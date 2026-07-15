namespace {
constexpr AccordSubscriptionId kInvalidSubscriptionId = 0;

bool isActiveState(AccordState state) {
	return state == AccordState::CollectingVotes || state == AccordState::Deferred;
}

bool isTerminalState(AccordState state) {
	return state == AccordState::Ready || state == AccordState::Rejected ||
	       state == AccordState::Failed || state == AccordState::Cancelled;
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

template <typename Callback>
struct AccordCallbackSlot {
	Callback callback;
	Callback pendingCallback;
	bool inFlight = false;
	bool hasPendingCallback = false;

	void set(Callback value) {
		if (inFlight) {
			pendingCallback = std::move(value);
			hasPendingCallback = true;
			return;
		}
		callback = std::move(value);
	}

	void finishInvocation() {
		inFlight = false;
		if (!hasPendingCallback) {
			return;
		}
		callback = std::move(pendingCallback);
		pendingCallback = nullptr;
		hasPendingCallback = false;
	}

	void clear() {
		callback = nullptr;
		pendingCallback = nullptr;
		inFlight = false;
		hasPendingCallback = false;
	}
};

struct AccordSubscriptionRecord {
	AccordSubscriptionId id = kInvalidSubscriptionId;
	uint32_t generation = 0;
	bool active = false;
	bool inFlight = false;
	bool pendingClear = false;
	AccordRequestCallback callback;

	bool available() const {
		return !active && !inFlight && !pendingClear;
	}

	void clearStorage() {
		id = kInvalidSubscriptionId;
		generation = 0;
		active = false;
		pendingClear = false;
		callback = nullptr;
	}

	void remove() {
		active = false;
		if (inFlight) {
			pendingClear = true;
			return;
		}
		clearStorage();
	}

	void finishInvocation() {
		inFlight = false;
		if (pendingClear) {
			clearStorage();
		}
	}
};

struct AccordSubscriptionKey {
	AccordSubscriptionId id = kInvalidSubscriptionId;
	uint32_t generation = 0;
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
	uint32_t completionId = 0;
};

struct AccordImpl {
	AccordConfig config{};
	AccordMutex mutex;
	AccordMutex callbackGate;
	std::unique_ptr<AccordSubscriptionRecord[]> subscribers;
	std::unique_ptr<AccordSubscriptionKey[]> voteSnapshot;
	size_t subscriberCapacity = 0;
	size_t activeSubscriberCount = 0;
	AccordCallbackSlot<AccordReadyCallback> readyCallback;
	AccordCallbackSlot<AccordRejectedCallback> rejectedCallback;
	AccordCallbackSlot<AccordDeferredCallback> deferredCallback;
	AccordCallbackSlot<AccordFailedCallback> failedCallback;
	AccordCallbackSlot<AccordCancelledCallback> cancelledCallback;
	bool initialized = false;
	bool deinitializing = false;
	bool pendingDeinit = false;
	bool processingVotes = false;
	uint32_t callbackDepth = 0;
	AccordState state = AccordState::Idle;
	AccordRequestInfo info{};
	bool lastInfoValid = false;
	AccordRequestInfo lastInfo{};
	AccordState lastFinishState = AccordState::Idle;
	AccordError lastError = AccordError::None;
	uint32_t requestId = 0;
	uint32_t completionId = 0;
	bool terminalEventPending = false;
	bool terminalEventInFlight = false;
	AccordFinishEvent pendingTerminalEvent{};
	AccordSubscriptionId nextSubscriptionId = 1;
	uint32_t subscriptionGeneration = 0;
	uint32_t retryStartedAtMs = 0;
	uint32_t retryAfterMs = 0;

	void clearRequestLocked() {
		info = AccordRequestInfo();
		retryStartedAtMs = 0;
		retryAfterMs = 0;
		state = AccordState::Idle;
	}

	void clearSubscribersLocked() {
		if (subscribers) {
			for (size_t i = 0; i < subscriberCapacity; ++i) {
				subscribers[i].clearStorage();
				subscribers[i].inFlight = false;
			}
		}
		activeSubscriberCount = 0;
	}

	AccordSubscriptionRecord *findSubscriberLocked(
	    AccordSubscriptionId id,
	    uint32_t generation,
	    bool requireActive
	) {
		if (id == kInvalidSubscriptionId || !subscribers) {
			return nullptr;
		}
		for (size_t i = 0; i < subscriberCapacity; ++i) {
			AccordSubscriptionRecord &record = subscribers[i];
			if (record.id != id) {
				continue;
			}
			if (generation != 0 && record.generation != generation) {
				continue;
			}
			if (requireActive && !record.active) {
				continue;
			}
			return &record;
		}
		return nullptr;
	}

	bool allocateSubscriptionIdLocked(AccordSubscriptionId &out) {
		for (uint32_t attempts = 0; attempts < UINT32_MAX; ++attempts) {
			const AccordSubscriptionId id = nextSubscriptionId++;
			if (id == kInvalidSubscriptionId) {
				continue;
			}
			if (findSubscriberLocked(id, 0, false) == nullptr) {
				out = id;
				return true;
			}
		}
		return false;
	}

	AccordFinishEvent makeEventLocked(
	    AccordFinishKind kind,
	    AccordError error = AccordError::None
	) const {
		AccordFinishEvent event;
		event.kind = kind;
		event.info = info;
		event.error = error;
		event.retryAfterMs = retryAfterMs;
		event.completionId = completionId;
		return event;
	}
};

namespace {
AccordState stateForFinishKind(AccordFinishKind kind) {
	switch (kind) {
		case AccordFinishKind::Ready:
			return AccordState::Ready;
		case AccordFinishKind::Rejected:
			return AccordState::Rejected;
		case AccordFinishKind::Failed:
			return AccordState::Failed;
		case AccordFinishKind::Cancelled:
			return AccordState::Cancelled;
		case AccordFinishKind::Deferred:
			return AccordState::Deferred;
		case AccordFinishKind::None:
			return AccordState::Idle;
	}
	return AccordState::Idle;
}

bool isTerminalKind(AccordFinishKind kind) {
	return kind == AccordFinishKind::Ready || kind == AccordFinishKind::Rejected ||
	       kind == AccordFinishKind::Failed || kind == AccordFinishKind::Cancelled;
}

void finishRequestLocked(
    AccordImpl &impl,
    AccordFinishEvent &event,
    AccordFinishKind kind,
    AccordError error = AccordError::None
) {
	impl.state = stateForFinishKind(kind);
	impl.lastInfo = impl.info;
	impl.lastInfoValid = true;
	impl.lastFinishState = impl.state;
	impl.lastError = error;
	impl.completionId = nextNonZero(impl.completionId);
	event = impl.makeEventLocked(kind, error);
	impl.pendingTerminalEvent = event;
	impl.terminalEventPending = true;
	impl.terminalEventInFlight = false;
	impl.requestId = nextNonZero(impl.requestId);
}

bool finishTimeoutIfElapsedLocked(AccordImpl &impl, AccordFinishEvent &event) {
	if (!isActiveState(impl.state) ||
	    !timeoutElapsed(impl.info.startedAtMs, impl.config.defaultTimeoutMs)) {
		return false;
	}
	finishRequestLocked(
	    impl,
	    event,
	    AccordFinishKind::Failed,
	    AccordError::RequestTimeout
	);
	return true;
}

void completeDeinitLocked(AccordImpl &impl) {
	impl.requestId = nextNonZero(impl.requestId);
	impl.processingVotes = false;
	impl.terminalEventPending = false;
	impl.terminalEventInFlight = false;
	impl.pendingTerminalEvent = AccordFinishEvent();
	impl.clearRequestLocked();
	impl.clearSubscribersLocked();
	impl.readyCallback.clear();
	impl.rejectedCallback.clear();
	impl.deferredCallback.clear();
	impl.failedCallback.clear();
	impl.cancelledCallback.clear();
	impl.subscribers.reset();
	impl.voteSnapshot.reset();
	impl.subscriberCapacity = 0;
	impl.initialized = false;
	impl.deinitializing = false;
	impl.pendingDeinit = false;
}

void completePendingDeinitIfReady(AccordImpl &impl) {
	if (impl.callbackDepth != 0) {
		return;
	}
	AccordLock lock(impl.mutex);
	if (lock && impl.pendingDeinit) {
		completeDeinitLocked(impl);
	}
}

template <typename Callback, typename Invoker>
void invokeLifecycleSlot(
    AccordImpl &impl,
    AccordCallbackSlot<Callback> &slot,
    Invoker invoker
) {
	bool shouldInvoke = false;
	{
		AccordLock lock(impl.mutex);
		if (!lock) {
			return;
		}
		if (slot.callback) {
			slot.inFlight = true;
			shouldInvoke = true;
		}
	}
	if (!shouldInvoke) {
		return;
	}

	impl.callbackDepth++;
	invoker(slot.callback);
	{
		AccordLock lock(impl.mutex);
		if (lock) {
			slot.finishInvocation();
		}
	}
	if (impl.callbackDepth > 0) {
		impl.callbackDepth--;
	}
	completePendingDeinitIfReady(impl);
}

void invokeEventWithGateHeld(AccordImpl &impl, const AccordFinishEvent &event) {
	const bool terminal = isTerminalKind(event.kind);
	if (terminal) {
		AccordLock lock(impl.mutex);
		if (!lock || !impl.terminalEventPending || impl.terminalEventInFlight ||
		    impl.pendingTerminalEvent.completionId != event.completionId) {
			return;
		}
		impl.terminalEventInFlight = true;
	}

	switch (event.kind) {
		case AccordFinishKind::Ready:
			invokeLifecycleSlot(
			    impl,
			    impl.readyCallback,
			    [&event](AccordReadyCallback &callback) {
				    callback(event.info);
			    }
			);
			break;
		case AccordFinishKind::Rejected:
			invokeLifecycleSlot(
			    impl,
			    impl.rejectedCallback,
			    [&event](AccordRejectedCallback &callback) {
				    callback(event.info);
			    }
			);
			break;
		case AccordFinishKind::Deferred:
			invokeLifecycleSlot(
			    impl,
			    impl.deferredCallback,
			    [&event](AccordDeferredCallback &callback) {
				    callback(event.info, event.retryAfterMs);
			    }
			);
			break;
		case AccordFinishKind::Failed:
			invokeLifecycleSlot(
			    impl,
			    impl.failedCallback,
			    [&event](AccordFailedCallback &callback) {
				    callback(event.info, event.error);
			    }
			);
			break;
		case AccordFinishKind::Cancelled:
			invokeLifecycleSlot(
			    impl,
			    impl.cancelledCallback,
			    [&event](AccordCancelledCallback &callback) {
				    callback(event.info);
			    }
			);
			break;
		case AccordFinishKind::None:
			break;
	}

	AccordLock lock(impl.mutex);
	if (!lock) {
		return;
	}
	if (terminal && impl.terminalEventPending &&
	    impl.pendingTerminalEvent.completionId == event.completionId) {
		impl.terminalEventPending = false;
		impl.terminalEventInFlight = false;
		impl.pendingTerminalEvent = AccordFinishEvent();
	}
	if (impl.pendingDeinit) {
		if (impl.callbackDepth == 0) {
			completeDeinitLocked(impl);
		}
		return;
	}
	if (terminal && impl.completionId == event.completionId && isTerminalState(impl.state)) {
		impl.clearRequestLocked();
	}
}

struct AccordProcessingGuard {
	AccordImpl &impl;

	~AccordProcessingGuard() {
		AccordLock lock(impl.mutex);
		if (lock) {
			impl.processingVotes = false;
		}
	}
};
} // namespace
