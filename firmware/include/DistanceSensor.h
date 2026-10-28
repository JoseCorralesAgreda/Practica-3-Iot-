#pragma once

#include <Arduino.h>

/** Capture one HC-SR04 measurement per second without waiting for the echo. */
class DistanceSensor {
public:
    /** Create an inactive sensor; hardware is configured by begin(). */
    DistanceSensor() = default;

    /** Configure the sensor pins and attach the echo interrupt. */
    void begin();

    /** Start due measurements and collect completed captures or timeouts. */
    void update();

    /** Consume one new valid reading; leave the output unchanged if absent. */
    bool takeReading(float& distanceCm);

private:
    static void ARDUINO_ISR_ATTR onEchoChange(void* context);
    void ARDUINO_ISR_ATTR handleEchoChange();
    void startMeasurement(uint32_t nowMs);

    portMUX_TYPE captureMutex_ = portMUX_INITIALIZER_UNLOCKED;
    bool captureActive_ = false;
    bool riseSeen_ = false;
    bool captureComplete_ = false;
    uint32_t triggerTimeUs_ = 0;
    uint32_t echoRiseUs_ = 0;
    uint32_t echoDurationUs_ = 0;
    uint32_t lastMeasurementMs_ = 0;
    bool readingAvailable_ = false;
    float distanceCm_ = 0.0f;
};
