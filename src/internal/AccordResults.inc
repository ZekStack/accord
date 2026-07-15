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

AccordSubscription::AccordSubscription(
    Accord *accord,
    AccordSubscriptionId id,
    uint32_t generation
)
    : _accord(accord), _id(id), _generation(generation) {
}

AccordSubscription::~AccordSubscription() = default;

AccordSubscription::AccordSubscription(AccordSubscription &&other) noexcept {
	_accord = other._accord;
	_id = other._id;
	_generation = other._generation;
	other._accord = nullptr;
	other._id = kInvalidSubscriptionId;
	other._generation = 0;
}

AccordSubscription &AccordSubscription::operator=(AccordSubscription &&other) noexcept {
	if (this == &other) {
		return *this;
	}
	_accord = other._accord;
	_id = other._id;
	_generation = other._generation;
	other._accord = nullptr;
	other._id = kInvalidSubscriptionId;
	other._generation = 0;
	return *this;
}

AccordResult AccordSubscription::unsubscribe() {
	if (_accord == nullptr || _id == kInvalidSubscriptionId) {
		return AccordResult::success("accord subscription handle is empty");
	}
	Accord *accord = _accord;
	const AccordSubscriptionId id = _id;
	const uint32_t generation = _generation;
	_accord = nullptr;
	_id = kInvalidSubscriptionId;
	_generation = 0;
	return generation == 0 ? accord->unsubscribe(id) : accord->unsubscribe(id, generation);
}

AccordSubscriptionId AccordSubscription::release() {
	const AccordSubscriptionId id = _id;
	_accord = nullptr;
	_id = kInvalidSubscriptionId;
	_generation = 0;
	return id;
}
