#include "MqttClient.h"

#include <cstring>

namespace {
constexpr unsigned long kSerialBaudRate = 115200;
constexpr uint32_t kReadingIntervalMs = 1000;
constexpr float kTestDistanceCm = 42.7f;
constexpr char kCommandTopic[] = "sis234/grupo2/leds/cmd";
constexpr char kValidCommand[] = "{\"led\":\"red\",\"state\":\"on\"}";
uint32_t lastReadingMs = 0;
unsigned int appliedCommands = 0;
char lastColor[7] = {};
bool lastEnabled = false;
bool checking = true;
bool checksPassed = false;

bool readDistance(float& distanceCm) {
    const uint32_t now = millis();
    if (now - lastReadingMs < kReadingIntervalMs) {
        return false;
    }
    lastReadingMs = now;
    distanceCm = kTestDistanceCm;
    return true;
}

bool applyLed(const char* color, bool enabled) {
    ++appliedCommands;
    snprintf(lastColor, sizeof(lastColor), "%s", color);
    lastEnabled = enabled;
    if (!checking) {
        Serial.printf("LED command: %s %s\n", color, enabled ? "on" : "off");
    }
    return true;
}

MqttClient mqttClient(readDistance, applyLed);
}

/** Exercise the real parser and receive queue before starting networking. */
struct MqttClientChecks {
    unsigned int passed = 0;
    unsigned int failed = 0;

    /** Record one assertion without aborting the remaining checks. */
    void expect(bool condition, const char* name) {
        if (condition) {
            ++passed;
        } else {
            ++failed;
            Serial.printf("FAIL: %s\n", name);
        }
    }

    /** Deliver a synthetic MQTT data event using explicit fragment lengths. */
    void fragment(const char* payload, int length, int offset, int total,
                  bool retained = false) {
        esp_mqtt_event_t event = {};
        event.topic = const_cast<char*>(kCommandTopic);
        event.topic_len = strlen(kCommandTopic);
        event.data = const_cast<char*>(payload);
        event.data_len = length;
        event.current_data_offset = offset;
        event.total_data_len = total;
        event.retain = retained;
        mqttClient.handleMessage(event);
    }

    /** Deliver and consume one complete command. */
    void message(const char* payload, bool retained = false) {
        const int length = strlen(payload);
        fragment(payload, length, 0, length, retained);
        mqttClient.processPendingCommands();
    }

    /** Check parsing, bounded reception and session isolation without WiFi. */
    bool run() {
        mqttClient.commandQueue_ = xQueueCreateStatic(
            MqttClient::kCommandQueueLength, sizeof(MqttClient::Command),
            mqttClient.commandQueueStorage_, &mqttClient.commandQueueControl_);
        mqttClient.state_ = MqttClient::State::Ready;
        const char* colors[] = {"red", "yellow", "green"};
        const char* states[] = {"on", "off"};
        for (const char* color : colors) {
            for (const char* state : states) {
                char payload[80];
                snprintf(payload, sizeof(payload), "{\"led\":\"%s\",\"state\":\"%s\"}", color, state);
                const unsigned int before = appliedCommands;
                message(payload);
                message(payload);
                expect(appliedCommands == before + 2 && strcmp(lastColor, color) == 0 &&
                       lastEnabled == (strcmp(state, "on") == 0), "valid repeated command");
            }
        }
        const char* invalid[] = {
            "", "{", "[]", "{}", "null", "{\"led\":\"red\"}",
            "{\"led\":\"blue\",\"state\":\"on\"}",
            "{\"led\":\"red\",\"state\":\"ON\"}",
            "{\"led\":\"red\",\"state\":true}",
            "{\"led\":\"red\",\"state\":1}",
            "{\"led\":\"red\",\"state\":null}",
            "{\"led\":\"red\",\"state\":{}}",
            "{\"led\":\"red\",\"state\":\"on\",\"extra\":\"x\"}",
            "{\"led\":\"red\",\"state\":\"on\",\"state\":\"off\"}",
            "{\"led\":\"red\",\"state\":\"on\",}",
            "{\"led\\u0000x\":\"red\",\"state\":\"on\"}",
            "{\"led\":\"red\",\"state\":\"on\"} garbage",
            "{\"led\":\"red\",\"state\":\"on\"}{}",
            "{'led':'red','state':'on'}",
            "{led:\"red\",state:\"on\"}",
            "{\"led\":\"red\\u0000blue\",\"state\":\"on\"}",
            "{\"led\":\"red\",\"state\":\"on\\u0000off\"}",
            "{\"led\":\"r}ed\",\"state\":\"on\"}",
            "{\"led\":\"r\\\"ed\",\"state\":\"on\"}"
        };
        for (const char* payload : invalid) {
            const unsigned int before = appliedCommands;
            message(payload);
            expect(appliedCommands == before, payload);
        }
        unsigned int before = appliedCommands;
        message(" \r\n{\"state\":\"on\",\"led\":\"red\"}\t ");
        expect(appliedCommands == before + 1, "outer whitespace and key order");

        const int length = strlen(kValidCommand);
        before = appliedCommands;
        fragment(kValidCommand, 5, 0, length);
        mqttClient.processPendingCommands();
        expect(appliedCommands == before, "incomplete fragment");
        fragment(kValidCommand + 5, length - 5, 5, length);
        mqttClient.processPendingCommands();
        expect(appliedCommands == before + 1, "fragment reassembly");

        before = appliedCommands;
        message(kValidCommand, true);
        fragment(kValidCommand, 5, 0, length);
        fragment(kValidCommand + 6, length - 6, 6, length);
        fragment(kValidCommand, 5, 0, length);
        fragment(kValidCommand + 5, length - 5, 5, length + 1);
        mqttClient.processPendingCommands();
        expect(appliedCommands == before, "retained, offset and total rejection");

        char oversized[130];
        memset(oversized, ' ', sizeof(oversized));
        memcpy(oversized, kValidCommand, length);
        oversized[129] = '\0';
        message(oversized);
        expect(appliedCommands == before, "oversized command");

        oversized[128] = '\0';
        message(oversized);
        expect(appliedCommands == before + 1, "maximum-size command");
        before = appliedCommands;

        fragment(kValidCommand, 5, 0, length);
        mqttClient.assembly_.receivedMs = millis() - 1001;
        fragment(kValidCommand + 5, length - 5, 5, length);
        mqttClient.processPendingCommands();
        expect(appliedCommands == before, "expired assembly");

        fragment(kValidCommand, 5, 0, length);
        ++mqttClient.generation_;
        fragment(kValidCommand + 5, length - 5, 5, length);
        mqttClient.processPendingCommands();
        expect(appliedCommands == before, "previous-session fragment");

        fragment(kValidCommand, length, 0, length);
        ++mqttClient.generation_;
        mqttClient.processPendingCommands();
        expect(appliedCommands == before, "previous-session command");

        fragment(kValidCommand, length, 0, length);
        MqttClient::Command command;
        xQueueReceive(mqttClient.commandQueue_, &command, 0);
        command.receivedMs = millis() - 1001;
        xQueueSend(mqttClient.commandQueue_, &command, 0);
        mqttClient.processPendingCommands();
        expect(appliedCommands == before, "expired queued command");

        for (size_t index = 0; index < MqttClient::kCommandQueueLength + 1; ++index) {
            fragment(kValidCommand, length, 0, length);
        }
        expect(uxQueueMessagesWaiting(mqttClient.commandQueue_) == 8, "bounded queue");
        mqttClient.processPendingCommands();
        expect(appliedCommands == before + 8, "queue does not overwrite commands");

        mqttClient.state_ = MqttClient::State::Subscribing;
        mqttClient.subscriptionId_ = 42;
        char grantedQos = 1;
        esp_mqtt_event_t subscribed = {};
        subscribed.msg_id = 41;
        subscribed.data = &grantedQos;
        subscribed.data_len = 1;
        mqttClient.handleSubscribe(subscribed);
        expect(mqttClient.state_ == MqttClient::State::Subscribing, "unmatched SUBACK");
        subscribed.msg_id = 42;
        mqttClient.handleSubscribe(subscribed);
        expect(mqttClient.state_ == MqttClient::State::Ready, "QoS 1 SUBACK");
        mqttClient.state_ = MqttClient::State::Subscribing;
        grantedQos = 0;
        mqttClient.handleSubscribe(subscribed);
        expect(mqttClient.restartRequested_, "rejected QoS grant");

        mqttClient.handleDisconnect();
        mqttClient.restartRequested_ = false;
        mqttClient.discarded_ = 0;
        mqttClient.assembling_ = false;
        appliedCommands = 0;
        checking = false;
        Serial.printf("MQTT checks: %u passed, %u failed\n", passed, failed);
        return failed == 0;
    }
};

/** Run deterministic receive checks, then start the real MQTT connection. */
void setup() {
    Serial.begin(kSerialBaudRate);
    MqttClientChecks checks;
    checksPassed = checks.run();
    if (checksPassed) {
        mqttClient.begin();
        Serial.println("MQTT check started: synthetic distance is 42.7 cm");
    }
}

/** Offer one synthetic reading per second and log broker-delivered commands. */
void loop() {
    if (checksPassed) {
        mqttClient.update();
    }
}
