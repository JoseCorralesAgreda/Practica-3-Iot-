#include "DistanceSensor.h"

#include <cmath>

namespace {
constexpr uint8_t kTriggerPin = 18;
constexpr uint8_t kEchoPin = 19;
constexpr uint32_t kMeasurementIntervalMs = 1000;
constexpr uint32_t kTriggerPulseUs = 10;
constexpr uint32_t kEchoTimeoutUs = 15000;
constexpr float kMinDistanceCm = 5.0f;
constexpr float kMaxDistanceCm = 200.0f;
constexpr float kSoundSpeedCmPerUs = 0.0343f;
constexpr float kRoundTripDivisor = 2.0f;
}

void DistanceSensor::begin() {
    pinMode(kTriggerPin, OUTPUT);
    digitalWrite(kTriggerPin, LOW);
    pinMode(kEchoPin, INPUT);

    portENTER_CRITICAL(&captureMutex_);
    captureActive_ = false;
    riseSeen_ = false;
    captureComplete_ = false;
    portEXIT_CRITICAL(&captureMutex_);

    readingAvailable_ = false;
    // Unsigned subtraction makes the first update due, including at boot.
    lastMeasurementMs_ = static_cast<uint32_t>(millis()) - kMeasurementIntervalMs;
    attachInterruptArg(kEchoPin, onEchoChange, this, CHANGE);
}

void DistanceSensor::update() {
    bool completed = false;
    uint32_t durationUs = 0;

    portENTER_CRITICAL(&captureMutex_);
    if (captureComplete_) {
        durationUs = echoDurationUs_;
        captureComplete_ = false;
        completed = true;
    } else if (captureActive_ &&
               static_cast<uint32_t>(micros() - triggerTimeUs_) >= kEchoTimeoutUs) {
        captureActive_ = false;
    }
    portEXIT_CRITICAL(&captureMutex_);

    if (completed) {
        const float distanceCm = durationUs * kSoundSpeedCmPerUs / kRoundTripDivisor;
        if (durationUs > 0 && std::isfinite(distanceCm) &&
            distanceCm >= kMinDistanceCm && distanceCm <= kMaxDistanceCm) {
            distanceCm_ = distanceCm;
            readingAvailable_ = true;
        }
    }

    const uint32_t nowMs = millis();
    if (static_cast<uint32_t>(nowMs - lastMeasurementMs_) >= kMeasurementIntervalMs) {
        startMeasurement(nowMs);
    }
}

bool DistanceSensor::takeReading(float& distanceCm) {
    if (!readingAvailable_) {
        return false;
    }
    distanceCm = distanceCm_;
    readingAvailable_ = false;
    return true;
}

void DistanceSensor::startMeasurement(uint32_t nowMs) {
    lastMeasurementMs_ = nowMs;
    readingAvailable_ = false;

    portENTER_CRITICAL(&captureMutex_);
    riseSeen_ = false;
    captureComplete_ = false;
    echoDurationUs_ = 0;
    triggerTimeUs_ = micros();
    captureActive_ = true;
    portEXIT_CRITICAL(&captureMutex_);

    digitalWrite(kTriggerPin, HIGH);
    // Only the 10 us trigger pulse waits; echo capture runs independently.
    delayMicroseconds(kTriggerPulseUs);
    digitalWrite(kTriggerPin, LOW);
}

void ARDUINO_ISR_ATTR DistanceSensor::onEchoChange(void* context) {
    static_cast<DistanceSensor*>(context)->handleEchoChange();
}

void ARDUINO_ISR_ATTR DistanceSensor::handleEchoChange() {
    const uint32_t nowUs = micros();
    const int level = digitalRead(kEchoPin);

    portENTER_CRITICAL_ISR(&captureMutex_);
    if (captureActive_) {
        // Reject late edges even when the main loop has not checked the timeout.
        if (static_cast<uint32_t>(nowUs - triggerTimeUs_) >= kEchoTimeoutUs) {
            captureActive_ = false;
        } else if (level == HIGH && !riseSeen_) {
            echoRiseUs_ = nowUs;
            riseSeen_ = true;
        } else if (level == LOW) {
            if (riseSeen_) {
                echoDurationUs_ = nowUs - echoRiseUs_;
                captureComplete_ = true;
            }
            captureActive_ = false;
        }
    }
    portEXIT_CRITICAL_ISR(&captureMutex_);
}
