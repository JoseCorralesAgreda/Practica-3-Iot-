#include <Arduino.h>

#include "DistanceSensor.h"
#include "LedController.h"
#include "MqttClient.h"

namespace {
constexpr unsigned long kSerialBaudRate = 115200;
DistanceSensor distanceSensor;
LedController ledController;

bool readDistance(float& distanceCm) {
    return distanceSensor.takeReading(distanceCm);
}

bool applyLed(const char* color, bool enabled) {
    return ledController.setLed(color, enabled);
}

MqttClient mqttClient(readDistance, applyLed);
}

/** Initialize hardware before starting connectivity. */
void setup() {
    Serial.begin(kSerialBaudRate);
    Serial.println("Smart object starting");
    ledController.begin();
    distanceSensor.begin();
    mqttClient.begin();
    Serial.println("Smart object setup complete");
}

/** Advance the sensor and pending commands without network waits. */
void loop() {
    distanceSensor.update();
    mqttClient.update();
}
