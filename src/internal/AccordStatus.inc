void Accord::loop() {
	if (!_impl) {
		return;
	}

	bool shouldRetry = false;
	AccordFinishEvent event;
	{
		AccordLock gate(_impl->callbackGate);
		if (!gate) {
			return;
		}
		{
			AccordLock lock(_impl->mutex);
			if (!lock || !_impl->initialized || _impl->deinitializing ||
			    !isActiveState(_impl->state)) {
				return;
			}
			if (!finishTimeoutIfElapsedLocked(*_impl, event) &&
			    _impl->state == AccordState::Deferred && !_impl->processingVotes &&
			    millis() - _impl->retryStartedAtMs >= _impl->retryAfterMs) {
				_impl->state = AccordState::CollectingVotes;
				shouldRetry = true;
			}
		}
		if (event.kind != AccordFinishKind::None) {
			invokeEventWithGateHeld(*_impl, event);
		}
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
	return lock && _impl->initialized && !_impl->deinitializing;
}

bool Accord::isRequestActive() const {
	if (!_impl) {
		return false;
	}
	AccordLock lock(_impl->mutex);
	return lock && _impl->initialized && !_impl->deinitializing && isActiveState(_impl->state);
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
	if (!lock || !_impl->initialized || _impl->deinitializing || !isActiveState(_impl->state)) {
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
	return unsubscribe(subscriptionId, 0);
}

AccordResult Accord::unsubscribe(
    AccordSubscriptionId subscriptionId,
    uint32_t generation
) {
	if (subscriptionId == kInvalidSubscriptionId) {
		return AccordResult::failure(AccordError::InvalidArgument, "subscription id is required");
	}
	if (!_impl) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord allocation failed");
	}

	{
		AccordLock lock(_impl->mutex);
		if (!lock) {
			return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
		}
		if (!_impl->initialized || _impl->deinitializing) {
			return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
		}

		AccordSubscriptionRecord *record =
		    _impl->findSubscriberLocked(subscriptionId, generation, true);
		if (record == nullptr) {
			return AccordResult::failure(
			    AccordError::SubscriptionNotFound,
			    "subscription not found"
			);
		}
		record->remove();
		if (_impl->activeSubscriberCount > 0) {
			_impl->activeSubscriberCount--;
		}
	}

	AccordLock gate(_impl->callbackGate);
	if (!gate) {
		return AccordResult::failure(AccordError::InternalError, "accord callback gate unavailable");
	}
	return AccordResult::success("accord subscription removed");
}
