#include "LedController.h"

#include <Arduino.h>
#include <cstring>

namespace {
constexpr uint8_t kRedLedPin = 25;
constexpr uint8_t kYellowLedPin = 26;
constexpr uint8_t kGreenLedPin = 27;
}

void LedController::begin() {
    // Preload low levels before enabling the outputs to avoid a startup flash.
    digitalWrite(kRedLedPin, LOW);
    digitalWrite(kYellowLedPin, LOW);
    digitalWrite(kGreenLedPin, LOW);
    pinMode(kRedLedPin, OUTPUT);
    pinMode(kYellowLedPin, OUTPUT);
    pinMode(kGreenLedPin, OUTPUT);
}

bool LedController::setLed(const char* color, bool enabled) {
    if (color == nullptr) {
        return false;
    }

    uint8_t pin;
    if (std::strcmp(color, "red") == 0) {
        pin = kRedLedPin;
    } else if (std::strcmp(color, "yellow") == 0) {
        pin = kYellowLedPin;
    } else if (std::strcmp(color, "green") == 0) {
        pin = kGreenLedPin;
    } else {
        return false;
    }

    digitalWrite(pin, enabled ? HIGH : LOW);
    return true;
}
