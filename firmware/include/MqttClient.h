#pragma once

#include <Arduino.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mqtt_client.h>

/** Bridge distance and LED adapters to MQTT without network waits in loop(). */
class MqttClient {
public:
    /** Store hardware-independent adapters; do not start networking. */
    MqttClient(bool (*readDistance)(float&), bool (*applyLed)(const char*, bool));
    /** Create fixed queues and start the network worker once. */
    void begin();
    /** Consume a reading and process at most eight commands without waiting. */
    void update();

private:
    friend struct MqttClientChecks;
    static constexpr size_t kMaxCommandBytes = 128;
    static constexpr size_t kCommandQueueLength = 8;
    static constexpr size_t kClientIdSize = 48;
    static constexpr size_t kNetworkStackWords = 2048;
    enum class State { Disconnected, Connecting, Subscribing, Ready };
    struct Command {
        char payload[kMaxCommandBytes + 1];
        size_t length;
        uint32_t receivedMs;
        uint32_t generation;
    };
    struct Reading {
        float distanceCm;
        uint32_t receivedMs;
        uint32_t generation;
    };

    static void networkTask(void* context);
    static void onEvent(void* context, esp_event_base_t base, int32_t id, void* data);
    void runNetwork();
    void updateConnections();
    void handleConnect();
    void handleDisconnect();
    void handleSubscribe(const esp_mqtt_event_t& event);
    void handleMessage(const esp_mqtt_event_t& event);
    void processPendingCommands();
    void publishReading();
    bool parseCommand(const Command& command, char* color, bool& enabled);
    void countDiscard();

    bool (*readDistance_)(float&);
    bool (*applyLed_)(const char*, bool);
    esp_mqtt_client_handle_t client_ = nullptr;
    char clientId_[kClientIdSize] = {};
    bool initialized_ = false;
    bool clientStarted_ = false;
    uint32_t lastWifiAttemptMs_ = 0;
    uint32_t lastMqttAttemptMs_ = 0;
    uint32_t lastDiagnosticMs_ = 0;

    portMUX_TYPE stateMutex_ = portMUX_INITIALIZER_UNLOCKED;
    State state_ = State::Disconnected;
    uint32_t generation_ = 0;
    uint32_t phaseStartedMs_ = 0;
    uint32_t discarded_ = 0;
    bool restartRequested_ = false;
    int subscriptionId_ = -1;

    // Only the MQTT event task assembles fragments.
    Command assembly_ = {};
    size_t assembledBytes_ = 0;
    bool assembling_ = false;
    QueueHandle_t commandQueue_ = nullptr;
    StaticQueue_t commandQueueControl_;
    uint8_t commandQueueStorage_[kCommandQueueLength * sizeof(Command)];
    QueueHandle_t readingQueue_ = nullptr;
    StaticQueue_t readingQueueControl_;
    uint8_t readingQueueStorage_[sizeof(Reading)];
    StaticTask_t networkTaskControl_;
    StackType_t networkStack_[kNetworkStackWords];
};
