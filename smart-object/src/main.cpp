#include <Arduino.h>

namespace {
constexpr unsigned long kHeartbeatIntervalMs = 1000;
unsigned long lastHeartbeatMs = 0;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Smart object started");
}

void loop() {
    const unsigned long now = millis();
    if (now - lastHeartbeatMs >= kHeartbeatIntervalMs) {
        lastHeartbeatMs = now;
        Serial.printf("Smart object is running | uptime_ms=%lu\n", now);
    }
    delay(10);
}
