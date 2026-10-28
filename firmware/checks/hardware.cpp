#include <Arduino.h>

#include "DistanceSensor.h"
#include "LedController.h"

namespace {
constexpr unsigned long kSerialBaudRate = 115200;
constexpr uint32_t kHeartbeatIntervalMs = 1000;
DistanceSensor distanceSensor;
LedController ledController;
uint32_t lastHeartbeatMs = 0;

void processSerialCommand(char command) {
    const char* color = nullptr;
    bool enabled = false;
    switch (command) {
        case 'r':
            color = "red";
            enabled = true;
            break;
        case 'R':
            color = "red";
            break;
        case 'y':
            color = "yellow";
            enabled = true;
            break;
        case 'Y':
            color = "yellow";
            break;
        case 'g':
            color = "green";
            enabled = true;
            break;
        case 'G':
            color = "green";
            break;
        case '\r':
        case '\n':
        case ' ':
            return;
        default:
            Serial.println("Unknown command. Use r/R, y/Y, g/G (on/off).");
            return;
    }
    if (ledController.setLed(color, enabled)) {
        Serial.printf("LED command: %s %s\n", color, enabled ? "on" : "off");
    }
}
}

/** Start the standalone hardware check without WiFi or MQTT. */
void setup() {
    Serial.begin(kSerialBaudRate);
    ledController.begin();
    distanceSensor.begin();
    Serial.println("Hardware check: r/R, y/Y, g/G switch LEDs on/off.");
    const bool rejectsUnknown = !ledController.setLed("blue", true);
    const bool rejectsNull = !ledController.setLed(nullptr, true);
    if (rejectsUnknown && rejectsNull) {
        Serial.println("Invalid color checks: PASS");
    } else {
        Serial.println("Invalid color checks: FAIL");
    }
}

/** Keep sampling and handle at most one serial character per iteration. */
void loop() {
    distanceSensor.update();
    float distanceCm;
    if (distanceSensor.takeReading(distanceCm)) {
        Serial.printf("Distance: %.1f cm\n", distanceCm);
    }
    if (Serial.available() > 0) {
        processSerialCommand(static_cast<char>(Serial.read()));
    }
    const uint32_t nowMs = millis();
    if (static_cast<uint32_t>(nowMs - lastHeartbeatMs) >= kHeartbeatIntervalMs) {
        lastHeartbeatMs = nowMs;
        // This remains visible when no valid echo is available.
        Serial.printf("Hardware check running | uptime_ms=%lu\n",
                      static_cast<unsigned long>(nowMs));
    }
}
