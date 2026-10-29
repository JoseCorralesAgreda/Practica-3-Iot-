#include "MqttClient.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "secrets.h"

namespace {
constexpr char kBrokerHost[] = "broker.hivemq.com";
constexpr uint16_t kBrokerPort = 1883;
constexpr char kDistanceTopic[] = "sis234/grupo2/distance";
constexpr char kCommandTopic[] = "sis234/grupo2/leds/cmd";
constexpr uint32_t kReconnectIntervalMs = 5000;
constexpr uint32_t kMqttAttemptTimeoutMs = 5000;
constexpr uint32_t kCommandMaxAgeMs = 1000;
constexpr uint32_t kDiagnosticIntervalMs = 1000;
constexpr uint32_t kNetworkPollMs = 20;
constexpr int kKeepAliveSeconds = 10;
constexpr size_t kDistancePayloadSize = 32;
constexpr size_t kColorSize = 7;
constexpr float kMinDistanceCm = 5.0f;
constexpr float kMaxDistanceCm = 200.0f;
constexpr UBaseType_t kNetworkPriority = 1;

bool isJsonWhitespace(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}
}

MqttClient::MqttClient(bool (*readDistance)(float&),
                       bool (*applyLed)(const char*, bool))
    : readDistance_(readDistance), applyLed_(applyLed) {}

void MqttClient::begin() {
    if (initialized_) {
        return;
    }
    initialized_ = true;
    Serial.println("MQTT client initializing");
    commandQueue_ = xQueueCreateStatic(kCommandQueueLength, sizeof(Command),
                                      commandQueueStorage_, &commandQueueControl_);
    readingQueue_ = xQueueCreateStatic(1, sizeof(Reading), readingQueueStorage_,
                                      &readingQueueControl_);
    if (readDistance_ == nullptr || applyLed_ == nullptr ||
        commandQueue_ == nullptr || readingQueue_ == nullptr) {
        Serial.println("MQTT initialization failed");
        return;
    }
    // ESP-IDF specifies task stack depth in bytes, unlike upstream FreeRTOS.
    const TaskHandle_t task = xTaskCreateStatic(
        networkTask, "mqtt-network", sizeof(networkStack_), this,
        kNetworkPriority, networkStack_, &networkTaskControl_);
    if (task == nullptr) {
        Serial.println("MQTT network task creation failed");
    }
}

void MqttClient::networkTask(void* context) {
    static_cast<MqttClient*>(context)->runNetwork();
    vTaskDelete(nullptr);
}

void MqttClient::runNetwork() {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    snprintf(clientId_, sizeof(clientId_),
             "sis234/grupo2/esp32/%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t config = {};
    config.host = kBrokerHost;
    config.port = kBrokerPort;
    config.client_id = clientId_;
    config.transport = MQTT_TRANSPORT_OVER_TCP;
    config.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    config.keepalive = kKeepAliveSeconds;
    config.disable_auto_reconnect = true;
    config.network_timeout_ms = kMqttAttemptTimeoutMs;
    config.reconnect_timeout_ms = kReconnectIntervalMs;
    client_ = esp_mqtt_client_init(&config);
    if (client_ == nullptr) {
        Serial.println("MQTT client allocation failed");
        return;
    }
    Serial.printf("MQTT client configured: broker=%s:%u client_id=%s\n",
                  kBrokerHost, kBrokerPort, clientId_);
    if (esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, onEvent, this) != ESP_OK) {
        Serial.println("MQTT event registration failed");
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        return;
    }
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    lastWifiAttemptMs_ = millis() - kReconnectIntervalMs;
    lastMqttAttemptMs_ = millis() - kReconnectIntervalMs;
    bool wasReady = false;
    for (;;) {
        updateConnections();
        portENTER_CRITICAL(&stateMutex_);
        const bool ready = state_ == State::Ready && !restartRequested_;
        portEXIT_CRITICAL(&stateMutex_);
        if (ready && !wasReady) {
            Serial.printf("MQTT ready: %s\n", clientId_);
        }
        wasReady = ready;
        publishReading();
        vTaskDelay(pdMS_TO_TICKS(kNetworkPollMs));
    }
}

void MqttClient::updateConnections() {
    const uint32_t now = millis();
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    portENTER_CRITICAL(&stateMutex_);
    const State state = state_;
    const bool expired = (state == State::Connecting || state == State::Subscribing) &&
                         now - phaseStartedMs_ >= kMqttAttemptTimeoutMs;
    const bool mustStop = restartRequested_ || expired || !wifiConnected;
    portEXIT_CRITICAL(&stateMutex_);

    if (clientStarted_ && mustStop) {
        // Stop waits for the SDK task, never for the hardware loop. It also
        // finishes an outstanding DNS/connect operation before another start.
        handleDisconnect();
        esp_mqtt_client_stop(client_);
        clientStarted_ = false;
        portENTER_CRITICAL(&stateMutex_);
        restartRequested_ = false;
        portEXIT_CRITICAL(&stateMutex_);
        xQueueReset(readingQueue_);
        Serial.println("MQTT disconnected; retry scheduled");
    }
    if (!wifiConnected) {
        if (millis() - lastWifiAttemptMs_ >= kReconnectIntervalMs) {
            lastWifiAttemptMs_ = millis();
            Serial.printf("WiFi connecting: ssid=%s\n", kWifiSsid);
            WiFi.disconnect();
            WiFi.begin(kWifiSsid, kWifiPassword);
        }
        return;
    }
    if (!clientStarted_ && millis() - lastMqttAttemptMs_ >= kReconnectIntervalMs) {
        lastMqttAttemptMs_ = millis();
        portENTER_CRITICAL(&stateMutex_);
        ++generation_;
        state_ = State::Connecting;
        phaseStartedMs_ = lastMqttAttemptMs_;
        restartRequested_ = false;
        subscriptionId_ = -1;
        portEXIT_CRITICAL(&stateMutex_);
        Serial.println("MQTT connecting");
        clientStarted_ = esp_mqtt_client_start(client_) == ESP_OK;
        if (!clientStarted_) {
            handleDisconnect();
        }
    }
}

void MqttClient::onEvent(void* context, esp_event_base_t, int32_t id, void* data) {
    auto* self = static_cast<MqttClient*>(context);
    const auto& event = *static_cast<esp_mqtt_event_t*>(data);
    switch (id) {
        case MQTT_EVENT_CONNECTED:
            self->handleConnect();
            break;
        case MQTT_EVENT_DISCONNECTED:
        case MQTT_EVENT_ERROR:
            self->handleDisconnect();
            break;
        case MQTT_EVENT_SUBSCRIBED:
            self->handleSubscribe(event);
            break;
        case MQTT_EVENT_DATA:
            self->handleMessage(event);
            break;
        default:
            break;
    }
}

void MqttClient::handleConnect() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&stateMutex_);
    const bool expected = state_ == State::Connecting && !restartRequested_;
    if (expected) {
        state_ = State::Subscribing;
        phaseStartedMs_ = now;
    }
    portEXIT_CRITICAL(&stateMutex_);
    if (!expected) {
        return;
    }
    Serial.println("MQTT connected; subscribing to LED commands");
    // SDK callbacks run serially, so SUBACK cannot overtake this assignment.
    const int packetId = esp_mqtt_client_subscribe(client_, kCommandTopic, 1);
    portENTER_CRITICAL(&stateMutex_);
    subscriptionId_ = packetId;
    if (packetId < 0) {
        restartRequested_ = true;
    }
    portEXIT_CRITICAL(&stateMutex_);
}

void MqttClient::handleDisconnect() {
    portENTER_CRITICAL(&stateMutex_);
    state_ = State::Disconnected;
    ++generation_;
    subscriptionId_ = -1;
    restartRequested_ = true;
    portEXIT_CRITICAL(&stateMutex_);
    Serial.println("MQTT event: disconnected or connection error");
}

void MqttClient::handleSubscribe(const esp_mqtt_event_t& event) {
    bool accepted = false;
    bool rejected = false;
    portENTER_CRITICAL(&stateMutex_);
    if (state_ == State::Subscribing && event.msg_id == subscriptionId_ &&
        !restartRequested_) {
        // One requested topic must have exactly one granted QoS byte.
        if (event.data != nullptr && event.data_len == 1 && event.data[0] == 1) {
            state_ = State::Ready;
            accepted = true;
        } else {
            restartRequested_ = true;
            rejected = true;
        }
    }
    portEXIT_CRITICAL(&stateMutex_);
    if (accepted) {
        Serial.println("MQTT subscription ready");
    } else if (rejected) {
        Serial.println("MQTT subscription rejected");
    }
}

void MqttClient::handleMessage(const esp_mqtt_event_t& event) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&stateMutex_);
    const bool ready = state_ == State::Ready && !restartRequested_;
    const uint32_t generation = generation_;
    portEXIT_CRITICAL(&stateMutex_);
    if (event.current_data_offset == 0) {
        assembling_ = false;
        assembledBytes_ = 0;
        const bool correctTopic = event.topic != nullptr &&
            event.topic_len == static_cast<int>(sizeof(kCommandTopic) - 1) &&
            memcmp(event.topic, kCommandTopic, sizeof(kCommandTopic) - 1) == 0;
        if (ready && correctTopic && !event.retain && event.total_data_len > 0 &&
            event.total_data_len <= static_cast<int>(kMaxCommandBytes)) {
            assembly_.length = event.total_data_len;
            assembly_.receivedMs = now;
            assembly_.generation = generation;
            assembling_ = true;
        }
    }
    if (!assembling_ || !ready || event.retain || event.data == nullptr ||
        event.data_len <= 0 || event.current_data_offset < 0 ||
        static_cast<size_t>(event.current_data_offset) != assembledBytes_ ||
        event.total_data_len != static_cast<int>(assembly_.length) ||
        static_cast<size_t>(event.data_len) > assembly_.length - assembledBytes_ ||
        assembly_.generation != generation || now - assembly_.receivedMs > kCommandMaxAgeMs) {
        assembling_ = false;
        countDiscard();
        return;
    }
    memcpy(assembly_.payload + assembledBytes_, event.data, event.data_len);
    assembledBytes_ += event.data_len;
    if (assembledBytes_ == assembly_.length) {
        assembly_.payload[assembly_.length] = '\0';
        assembling_ = false;
        if (xQueueSend(commandQueue_, &assembly_, 0) != pdTRUE) {
            countDiscard();
        }
    }
}

bool MqttClient::parseCommand(const Command& command, char* color, bool& enabled) {
    // ArduinoJson accepts a valid prefix. Check the entire flat object first,
    // including quoted braces, escaped quotes, duplicate keys and trailing bytes.
    bool quoted = false;
    bool escaped = false;
    bool opened = false;
    bool closed = false;
    size_t colonCount = 0;
    for (size_t index = 0; index < command.length; ++index) {
        const char value = command.payload[index];
        if (quoted) {
            if (static_cast<unsigned char>(value) < 0x20) {
                return false;
            }
            if (escaped) {
                // ArduinoJson can truncate object keys at a decoded NUL.
                if (value == 'u' && index + 4 < command.length &&
                    memcmp(command.payload + index + 1, "0000", 4) == 0) {
                    return false;
                }
                escaped = false;
            } else if (value == '\\') {
                escaped = true;
            } else if (value == '"') {
                quoted = false;
            }
        } else if (isJsonWhitespace(value)) {
            continue;
        } else if (!opened && value == '{') {
            opened = true;
        } else if (!opened || closed) {
            return false;
        } else if (value == '"') {
            quoted = true;
        } else if (value == '}') {
            closed = true;
        } else if (value == ':') {
            ++colonCount;
        } else if (value != ',') {
            return false;
        }
    }
    if (!closed || quoted || colonCount != 2) {
        return false;
    }
    StaticJsonDocument<256> document;
    const auto error = deserializeJson(document, command.payload, command.length,
                                       DeserializationOption::NestingLimit(1));
    if (error || !document.is<JsonObject>() || document.size() != 2 ||
        !document["led"].is<const char*>() || !document["state"].is<const char*>()) {
        return false;
    }
    for (JsonPairConst field : document.as<JsonObjectConst>()) {
        if (field.key().size() != strlen(field.key().c_str())) {
            return false;
        }
    }
    const JsonString led = document["led"].as<JsonString>();
    const JsonString state = document["state"].as<JsonString>();
    // Reject embedded NULs instead of accepting a truncated color or state.
    if (led.size() != strlen(led.c_str()) || state.size() != strlen(state.c_str())) {
        return false;
    }
    if (strcmp(led.c_str(), "red") != 0 && strcmp(led.c_str(), "yellow") != 0 &&
        strcmp(led.c_str(), "green") != 0) {
        return false;
    }
    if (strcmp(state.c_str(), "on") != 0 && strcmp(state.c_str(), "off") != 0) {
        return false;
    }
    snprintf(color, kColorSize, "%s", led.c_str());
    enabled = strcmp(state.c_str(), "on") == 0;
    return true;
}

void MqttClient::processPendingCommands() {
    Command command;
    for (size_t index = 0; index < kCommandQueueLength; ++index) {
        if (xQueueReceive(commandQueue_, &command, 0) != pdTRUE) {
            break;
        }
        char color[kColorSize];
        bool enabled = false;
        if (!parseCommand(command, color, enabled)) {
            Serial.println("MQTT command rejected: invalid JSON or LED state");
            countDiscard();
            continue;
        }
        portENTER_CRITICAL(&stateMutex_);
        const bool current = state_ == State::Ready && !restartRequested_ &&
                             command.generation == generation_;
        portEXIT_CRITICAL(&stateMutex_);
        if (!current || millis() - command.receivedMs > kCommandMaxAgeMs ||
            !applyLed_(color, enabled)) {
            Serial.println("MQTT command discarded: stale, disconnected, or unsupported");
            countDiscard();
        } else {
            Serial.printf("MQTT command applied: %s=%s\n",
                          color, enabled ? "on" : "off");
        }
    }
}

void MqttClient::publishReading() {
    Reading reading;
    if (xQueueReceive(readingQueue_, &reading, 0) != pdTRUE) {
        return;
    }
    portENTER_CRITICAL(&stateMutex_);
    const bool current = state_ == State::Ready && !restartRequested_ &&
                         reading.generation == generation_;
    portEXIT_CRITICAL(&stateMutex_);
    if (!current || millis() - reading.receivedMs >= kCommandMaxAgeMs) {
        return;
    }
    char payload[kDistancePayloadSize];
    const int length = snprintf(payload, sizeof(payload), "{\"cm\":%.1f}", reading.distanceCm);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(payload)) {
        countDiscard();
        return;
    }
    // QoS 0 sends once and never adds a sample to the SDK retransmission outbox.
    if (esp_mqtt_client_publish(client_, kDistanceTopic, payload, length, 0, false) < 0) {
        Serial.println("MQTT distance publish failed");
        countDiscard();
    } else {
        Serial.printf("MQTT distance published: %s\n", payload);
    }
}

void MqttClient::countDiscard() {
    portENTER_CRITICAL(&stateMutex_);
    ++discarded_;
    portEXIT_CRITICAL(&stateMutex_);
}

void MqttClient::update() {
    if (commandQueue_ == nullptr || readingQueue_ == nullptr ||
        readDistance_ == nullptr || applyLed_ == nullptr) {
        return;
    }
    processPendingCommands();
    Reading reading = {};
    if (readDistance_(reading.distanceCm)) {
        reading.receivedMs = millis();
        portENTER_CRITICAL(&stateMutex_);
        const bool ready = state_ == State::Ready && !restartRequested_;
        reading.generation = generation_;
        portEXIT_CRITICAL(&stateMutex_);
        if (ready && std::isfinite(reading.distanceCm) &&
            reading.distanceCm >= kMinDistanceCm && reading.distanceCm <= kMaxDistanceCm) {
            xQueueOverwrite(readingQueue_, &reading);
        }
    }
    const uint32_t now = millis();
    if (now - lastDiagnosticMs_ >= kDiagnosticIntervalMs) {
        lastDiagnosticMs_ = now;
        portENTER_CRITICAL(&stateMutex_);
        const uint32_t discarded = discarded_;
        discarded_ = 0;
        portEXIT_CRITICAL(&stateMutex_);
        if (discarded > 0) {
            Serial.printf("MQTT discarded items: %lu\n", static_cast<unsigned long>(discarded));
        }
    }
}
