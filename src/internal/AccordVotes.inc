AccordResult Accord::processVotes() {
	if (!_impl) {
		return AccordResult::failure(AccordError::OutOfMemory, "accord allocation failed");
	}
	AccordImpl &impl = *_impl;
	size_t snapshotCount = 0;
	const char *label = nullptr;
	uint32_t retryCount = 0;
	uint32_t requestId = 0;
	AccordFinishEvent earlyEvent;
	AccordResult earlyResult = AccordResult::success();

	{
		AccordLock gate(impl.callbackGate);
		if (!gate) {
			return AccordResult::failure(
			    AccordError::InternalError,
			    "accord callback gate unavailable"
			);
		}
		{
			AccordLock lock(impl.mutex);
			if (!lock) {
				return AccordResult::failure(AccordError::InternalError, "accord mutex unavailable");
			}
			if (!impl.initialized || impl.deinitializing) {
				return AccordResult::failure(AccordError::NotInitialized, "accord is not initialized");
			}
			if (impl.state != AccordState::CollectingVotes) {
				return AccordResult::failure(
				    AccordError::Cancelled,
				    "accord request is no longer active"
				);
			}
			if (impl.processingVotes) {
				return AccordResult::failure(
				    AccordError::RequestAlreadyActive,
				    "accord vote processing already active"
				);
			}
			impl.processingVotes = true;

			if (finishTimeoutIfElapsedLocked(impl, earlyEvent)) {
				earlyResult = AccordResult::failure(
				    AccordError::RequestTimeout,
				    "accord request timed out"
				);
			} else {
				snapshotCount = impl.activeSubscriberCount;
			}
			if (earlyEvent.kind == AccordFinishKind::None && snapshotCount == 0) {
				const bool allow = impl.config.allowWithoutSubscribers;
				finishRequestLocked(
				    impl,
				    earlyEvent,
				    allow ? AccordFinishKind::Ready : AccordFinishKind::Failed,
				    allow ? AccordError::None : AccordError::NoSubscribers
				);
				earlyResult = allow
				                  ? AccordResult::success("accord request ready")
				                  : AccordResult::failure(
				                        AccordError::NoSubscribers,
				                        "accord request failed"
				                    );
			}

			if (earlyEvent.kind == AccordFinishKind::None) {
				size_t writeIndex = 0;
				for (size_t i = 0; i < impl.subscriberCapacity && writeIndex < snapshotCount; ++i) {
					const AccordSubscriptionRecord &record = impl.subscribers[i];
					if (record.active && record.callback) {
						impl.voteSnapshot[writeIndex].id = record.id;
						impl.voteSnapshot[writeIndex].generation = record.generation;
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
			invokeEventWithGateHeld(impl, earlyEvent);
		}
	}

	AccordProcessingGuard processingGuard{impl};
	if (earlyEvent.kind != AccordFinishKind::None) {
		return earlyResult;
	}

	for (size_t i = 0; i < snapshotCount; ++i) {
		const AccordSubscriptionKey key = impl.voteSnapshot[i];
		AccordFinishEvent event;
		AccordError terminalError = AccordError::None;
		bool stale = false;
		bool shouldInvoke = false;
		AccordSubscriptionRecord *record = nullptr;

		AccordLock gate(impl.callbackGate);
		if (!gate) {
			return AccordResult::failure(
			    AccordError::InternalError,
			    "accord callback gate unavailable"
			);
		}
		{
			AccordLock lock(impl.mutex);
			if (!lock) {
				return AccordResult::failure(
				    AccordError::InternalError,
				    "accord mutex unavailable"
				);
			}
			if (!impl.initialized || impl.deinitializing || impl.requestId != requestId ||
			    impl.state != AccordState::CollectingVotes) {
				stale = true;
			} else if (finishTimeoutIfElapsedLocked(impl, event)) {
				terminalError = AccordError::RequestTimeout;
			} else {
				record = impl.findSubscriberLocked(key.id, key.generation, true);
				if (record != nullptr && record->callback) {
					record->inFlight = true;
					shouldInvoke = true;
				}
			}
		}

		if (event.kind != AccordFinishKind::None) {
			invokeEventWithGateHeld(impl, event);
			return AccordResult::failure(terminalError, "accord request failed");
		}
		if (stale) {
			return AccordResult::failure(AccordError::Cancelled, "accord request is no longer active");
		}
		if (!shouldInvoke || record == nullptr) {
			continue;
		}

		AccordRequest request(label, retryCount, requestId);
		impl.callbackDepth++;
		record->callback(request);

		{
			AccordLock lock(impl.mutex);
			if (!lock) {
				if (impl.callbackDepth > 0) {
					impl.callbackDepth--;
				}
				return AccordResult::failure(
				    AccordError::InternalError,
				    "accord mutex unavailable"
				);
			}
			AccordSubscriptionRecord *current =
			    impl.findSubscriberLocked(key.id, key.generation, false);
			if (!impl.initialized || impl.deinitializing || impl.requestId != requestId ||
			    impl.state != AccordState::CollectingVotes) {
				stale = true;
			} else if (finishTimeoutIfElapsedLocked(impl, event)) {
				terminalError = AccordError::RequestTimeout;
			} else if (current != nullptr && current->active) {
				if (!request.hasDecision()) {
					finishRequestLocked(
					    impl,
					    event,
					    AccordFinishKind::Failed,
					    AccordError::MissingVote
					);
					terminalError = AccordError::MissingVote;
				} else if (request.decision() == AccordDecision::Allow) {
					impl.info.allowCount++;
				} else if (request.decision() == AccordDecision::Reject) {
					impl.info.rejectCount++;
					if (impl.info.rejectMessage == nullptr && request.rejectMessage() != nullptr) {
						impl.info.rejectMessage = request.rejectMessage();
					}
				} else if (request.decision() == AccordDecision::Defer) {
					impl.info.deferCount++;
					impl.retryAfterMs = std::max(
					    impl.retryAfterMs,
					    clampDeferMs(request.retryAfterMs(), impl.config)
					);
				}
			}
			if (current != nullptr) {
				current->finishInvocation();
			}
		}

		if (impl.callbackDepth > 0) {
			impl.callbackDepth--;
		}
		completePendingDeinitIfReady(impl);

		if (event.kind != AccordFinishKind::None) {
			invokeEventWithGateHeld(impl, event);
			return AccordResult::failure(terminalError, "accord request failed");
		}
		if (stale) {
			return AccordResult::failure(AccordError::Cancelled, "accord request is no longer active");
		}
	}

	AccordFinishEvent event;
	AccordError error = AccordError::None;
	{
		AccordLock gate(impl.callbackGate);
		if (!gate) {
			return AccordResult::failure(
			    AccordError::InternalError,
			    "accord callback gate unavailable"
			);
		}
		{
			AccordLock lock(impl.mutex);
			if (!lock) {
				return AccordResult::failure(
				    AccordError::InternalError,
				    "accord mutex unavailable"
				);
			}
			if (!impl.initialized || impl.deinitializing || impl.requestId != requestId ||
			    impl.state != AccordState::CollectingVotes) {
				return AccordResult::failure(
				    AccordError::Cancelled,
				    "accord request is no longer active"
				);
			}
			if (finishTimeoutIfElapsedLocked(impl, event)) {
				error = AccordError::RequestTimeout;
			} else if (impl.info.rejectCount > 0) {
				finishRequestLocked(impl, event, AccordFinishKind::Rejected);
			} else if (impl.info.deferCount > 0) {
				if (impl.info.retryCount >= impl.config.maxRetries) {
					error = AccordError::MaxRetriesReached;
					finishRequestLocked(
					    impl,
					    event,
					    AccordFinishKind::Failed,
					    error
					);
				} else {
					impl.info.retryCount++;
					impl.state = AccordState::Deferred;
					impl.retryStartedAtMs = millis();
					event = impl.makeEventLocked(AccordFinishKind::Deferred);
				}
			} else {
				finishRequestLocked(impl, event, AccordFinishKind::Ready);
			}
		}
		invokeEventWithGateHeld(impl, event);
	}

	if (event.kind == AccordFinishKind::Failed) {
		return AccordResult::failure(error, "accord request failed");
	}
	return AccordResult::success("accord request accepted");
}
