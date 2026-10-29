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
    Serial.println("LED controller ready: red=off, yellow=off, green=off");
}

bool LedController::setLed(const char* color, bool enabled) {
    if (color == nullptr) {
        Serial.println("LED command rejected: null color");
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
        Serial.printf("LED command rejected: unknown color '%s'\n", color);
        return false;
    }

    digitalWrite(pin, enabled ? HIGH : LOW);
    Serial.printf("LED state changed: %s=%s\n", color, enabled ? "on" : "off");
    return true;
}
