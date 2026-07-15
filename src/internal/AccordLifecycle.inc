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

	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return AccordResult::failure(AccordError::InternalError, "accord callback gate unavailable");
	}
	AccordLock lock(_impl->mutex);
	if (!lock) {
		return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
	}
	if (_impl->initialized || _impl->deinitializing) {
		return AccordResult::failure(AccordError::AlreadyInitialized, "accord is already initialized");
	}

	std::unique_ptr<AccordSubscriptionRecord[]> subscribers(
	    new (std::nothrow) AccordSubscriptionRecord[config.maxSubscribers]
	);
	std::unique_ptr<AccordSubscriptionKey[]> voteSnapshot(
	    new (std::nothrow) AccordSubscriptionKey[config.maxSubscribers]
	);
	if (!subscribers || !voteSnapshot) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord storage allocation failed");
	}

	_impl->config = config;
	_impl->subscribers = std::move(subscribers);
	_impl->voteSnapshot = std::move(voteSnapshot);
	_impl->subscriberCapacity = config.maxSubscribers;
	_impl->activeSubscriberCount = 0;
	_impl->initialized = true;
	_impl->deinitializing = false;
	_impl->pendingDeinit = false;
	_impl->processingVotes = false;
	_impl->state = AccordState::Idle;
	_impl->info = AccordRequestInfo();
	_impl->lastInfoValid = false;
	_impl->lastInfo = AccordRequestInfo();
	_impl->lastFinishState = AccordState::Idle;
	_impl->lastError = AccordError::None;
	_impl->retryStartedAtMs = 0;
	_impl->retryAfterMs = 0;
	_impl->requestId = nextNonZero(_impl->requestId);
	_impl->subscriptionGeneration = nextNonZero(_impl->subscriptionGeneration);
	return AccordResult::success("accord initialized");
}

AccordResult Accord::deinit() {
	if (!_impl) {
		return AccordResult::success("accord deinitialized");
	}

	AccordFinishEvent event;
	bool alreadyDeinitializing = false;
	{
		AccordLock lock(_impl->mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		if (!_impl->initialized && !_impl->deinitializing) {
			return AccordResult::success("accord is not initialized");
		}
		alreadyDeinitializing = _impl->deinitializing;
		if (!alreadyDeinitializing) {
			if (isActiveState(_impl->state)) {
				finishRequestLocked(
				    *_impl,
				    event,
				    AccordFinishKind::Cancelled,
				    AccordError::Cancelled
				);
			}
			_impl->deinitializing = true;
			_impl->initialized = false;
		}
	}

	if (alreadyDeinitializing) {
		for (;;) {
			{
				AccordLock gate(_impl->callbackGate);
				if (!gate) {
					return AccordResult::failure(
					    AccordError::InternalError,
					    "accord callback gate unavailable"
					);
				}
				AccordLock lock(_impl->mutex);
				if (!lock) {
					return AccordResult::failure(
					    AccordError::InternalError,
					    "accord mutex unavailable"
					);
				}
				if (!_impl->deinitializing) {
					return AccordResult::success("accord deinitialized");
				}
				if (_impl->callbackDepth > 0) {
					_impl->pendingDeinit = true;
					return AccordResult::success(
					    "accord deinitialization deferred until callback exit"
					);
				}
			}
			taskYIELD();
		}
	}

	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return AccordResult::failure(AccordError::InternalError, "accord callback gate unavailable");
	}

	const bool deferredCleanup = _impl->callbackDepth > 0;
	{
		AccordLock lock(_impl->mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		_impl->pendingDeinit = deferredCleanup;
		if (_impl->terminalEventPending) {
			event = _impl->pendingTerminalEvent;
		}
	}

	if (event.kind != AccordFinishKind::None) {
		invokeEventWithGateHeld(*_impl, event);
	}
	if (deferredCleanup) {
		return AccordResult::success("accord deinitialization deferred until callback exit");
	}

	AccordLock lock(_impl->mutex);
	if (!lock) {
		return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
	}
	completeDeinitLocked(*_impl);
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
	bool immediateAllow = false;
	bool immediateFailure = false;
	{
		AccordLock gate(_impl->callbackGate);
		if (!gate) {
			return AccordResult::failure(
			    AccordError::InternalError,
			    "accord callback gate unavailable"
			);
		}
		{
			AccordLock lock(_impl->mutex);
			if (!lock) {
				return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
			}
			if (!_impl->initialized || _impl->deinitializing) {
				return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
			}
			if (_impl->state != AccordState::Idle) {
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
			_impl->retryStartedAtMs = 0;
			_impl->retryAfterMs = 0;

			if (_impl->activeSubscriberCount == 0) {
				immediateAllow = _impl->config.allowWithoutSubscribers;
				immediateFailure = !immediateAllow;
				finishRequestLocked(
				    *_impl,
				    event,
				    immediateAllow ? AccordFinishKind::Ready : AccordFinishKind::Failed,
				    immediateAllow ? AccordError::None : AccordError::NoSubscribers
				);
			}
		}
		if (event.kind != AccordFinishKind::None) {
			invokeEventWithGateHeld(*_impl, event);
		}
	}

	if (immediateAllow) {
		return AccordResult::success("accord request ready");
	}
	if (immediateFailure) {
		return AccordResult::failure(AccordError::NoSubscribers, "accord request failed");
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
		if (!_impl->initialized || _impl->deinitializing) {
			return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
		}
		if (!isActiveState(_impl->state)) {
			return AccordResult::success("accord request is not active");
		}
		finishRequestLocked(
		    *_impl,
		    event,
		    AccordFinishKind::Cancelled,
		    AccordError::Cancelled
		);
	}

	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return AccordResult::failure(AccordError::InternalError, "accord callback gate unavailable");
	}
	invokeEventWithGateHeld(*_impl, event);
	return AccordResult::success("accord request cancelled");
}

AccordResult Accord::force(const char *label) {
	if (isEmpty(label)) {
		return AccordResult::failure(AccordError::InvalidArgument, "force label is required");
	}
	if (!_impl) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord allocation failed");
	}

	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return AccordResult::failure(AccordError::InternalError, "accord callback gate unavailable");
	}
	AccordFinishEvent event;
	{
		AccordLock lock(_impl->mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		if (!_impl->initialized || _impl->deinitializing) {
			return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
		}
		if (_impl->state != AccordState::Idle) {
			return AccordResult::failure(
			    AccordError::RequestAlreadyActive,
			    "accord force request cannot interrupt active request"
			);
		}

		_impl->requestId = nextNonZero(_impl->requestId);
		_impl->state = AccordState::CollectingVotes;
		_impl->info = AccordRequestInfo();
		_impl->info.label = label;
		_impl->info.startedAtMs = millis();
		_impl->info.subscriberCount = _impl->activeSubscriberCount;
		finishRequestLocked(*_impl, event, AccordFinishKind::Ready);
	}
	invokeEventWithGateHeld(*_impl, event);
	return AccordResult::success("accord force request ready");
}

AccordSubscriptionResult Accord::subscribe(AccordRequestCallback callback) {
	if (!_impl || !callback) {
		return AccordSubscriptionResult::failure(
		    AccordError::InvalidArgument,
		    "accord request callback is required"
		);
	}

	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return AccordSubscriptionResult::failure(
		    AccordError::InternalError,
		    "accord callback gate unavailable"
		);
	}
	AccordLock lock(_impl->mutex);
	if (!lock) {
		return AccordSubscriptionResult::failure(
		    AccordError::InternalError,
		    "accord mutex unavailable"
		);
	}
	if (!_impl->initialized || _impl->deinitializing || !_impl->subscribers) {
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
	slot->generation = _impl->subscriptionGeneration;
	slot->active = true;
	slot->inFlight = false;
	slot->pendingClear = false;
	slot->callback = std::move(callback);
	_impl->activeSubscriberCount++;
	return AccordSubscriptionResult::success(
	    AccordSubscription(this, id, _impl->subscriptionGeneration)
	);
}

AccordSubscription Accord::onRequest(AccordRequestCallback callback) {
	AccordSubscriptionResult result = subscribe(std::move(callback));
	if (!result) {
		return AccordSubscription();
	}
	return std::move(result.subscription);
}

void Accord::onReady(AccordReadyCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->readyCallback.set(std::move(callback));
	}
}

void Accord::onRejected(AccordRejectedCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->rejectedCallback.set(std::move(callback));
	}
}

void Accord::onDeferred(AccordDeferredCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->deferredCallback.set(std::move(callback));
	}
}

void Accord::onFailed(AccordFailedCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->failedCallback.set(std::move(callback));
	}
}

void Accord::onCancelled(AccordCancelledCallback callback) {
	if (!_impl) {
		return;
	}
	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return;
	}
	AccordLock lock(_impl->mutex);
	if (lock) {
		_impl->cancelledCallback.set(std::move(callback));
	}
}
