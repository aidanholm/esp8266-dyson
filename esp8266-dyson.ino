#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

#include "dyson_uart.h"

WiFiManager wifiManager;
WiFiClient wifiClient;
PubSubClient mqttClient;

#define FIRMWARE_PREFIX "esp8266-dyson"

char identifier[24];
char MQTT_TOPIC_AVAILABILITY[128];
char MQTT_TOPIC_STATE[128];
char MQTT_TOPIC_AUTOCONF_FANSPEED_SENSOR[128];

const char *mqttUsername = "username";
const char *mqttPassword = "hunter2";
const char *mqttHost = "192.168.18.83";
const int mqttPort = 1883;

void setupWifi() {
    WiFi.hostname(identifier);
    wifiManager.autoConnect(identifier);
}

void setupMqtt() {
    mqttClient.setClient(wifiClient);
    mqttClient.setServer(mqttHost, mqttPort);
    mqttClient.setKeepAlive(10);
    mqttClient.setBufferSize(2048);
}

DysonUartParser parser;

double current_fan_speed = 0;
double last_sent_fan_speed = 0;
bool fan_speed_valid = false;

void publishState() {
    DynamicJsonDocument stateJson(604);

    {
        DynamicJsonDocument wifiJson(192);
        wifiJson["ssid"] = WiFi.SSID();
        wifiJson["ip"] = WiFi.localIP().toString();
        wifiJson["rssi"] = WiFi.RSSI();
        stateJson["wifi"] = wifiJson.as<JsonObject>();
    }

    stateJson["fan_speed"] = String(current_fan_speed);

    char payload[256];
    serializeJson(stateJson, payload);
    mqttClient.publish(MQTT_TOPIC_STATE, payload, true);
}

void publishAutoConfig() {
    char mqttPayload[2048];
    DynamicJsonDocument device(256);
    DynamicJsonDocument autoconfPayload(1024);
    StaticJsonDocument<64> identifiersDoc;
    JsonArray identifiers = identifiersDoc.to<JsonArray>();

    identifiers.add(identifier);

    device["identifiers"] = identifiers;
    device["manufacturer"] = "Dyson";
    device["model"] = "Purifier Cool (TP10)";
    device["name"] = identifier;
    device["sw_version"] = "2025.05.30";

    autoconfPayload["device"] = device.as<JsonObject>();
    autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
    autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
    autoconfPayload["name"] = identifier + String(" fan speed");
    autoconfPayload["value_template"] = "{{value_json.fan_speed}}";
    autoconfPayload["unique_id"] = identifier + String("_fan_speed");
    autoconfPayload["icon"] = "mdi:fan";

    serializeJson(autoconfPayload, mqttPayload);
    mqttClient.publish(MQTT_TOPIC_AUTOCONF_FANSPEED_SENSOR, mqttPayload, true);

    autoconfPayload.clear();
}

void mqttReconnect() {
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (mqttClient.connect(identifier, mqttUsername, mqttPassword,
                               MQTT_TOPIC_AVAILABILITY, 1, true, "offline")) {
            mqttClient.publish(MQTT_TOPIC_AVAILABILITY, "online", true);
            publishAutoConfig();
            return;
        }
    }
}

void setup() {
    snprintf(identifier, sizeof(identifier), "Dyson-%X", ESP.getChipId());
    snprintf(MQTT_TOPIC_AVAILABILITY, 127, "%s/%s/status", FIRMWARE_PREFIX,
             identifier);
    snprintf(MQTT_TOPIC_STATE, 127, "%s/%s/state", FIRMWARE_PREFIX, identifier);
    snprintf(MQTT_TOPIC_AUTOCONF_FANSPEED_SENSOR, 127,
             "homeassistant/sensor/%s/%s_fan_speed/config", FIRMWARE_PREFIX,
             identifier);

    setupWifi();
    setupMqtt();

    // TODO: improve (re)connection behavior.
    mqttReconnect();

    Serial.begin(115200);
    Serial.swap();  // Required for the D1 mini.
}

void handleUart() {
    while (Serial.available()) {
        DysonParseResult_t result;
        parser.putch(Serial.read(), &result);
        if (result.success && result.field_id == 0x00040002 &&
            result.param_count >= 5 && result.data_type == DataType_t::DOUBLE) {
            current_fan_speed = result.value.d64s[4];
            fan_speed_valid = true;
        }
    }
}

void maybePublish() {
    if (fan_speed_valid && current_fan_speed != last_sent_fan_speed) {
        publishState();
        last_sent_fan_speed = current_fan_speed;
    }
}

void loop() {
    handleUart();
    mqttClient.loop();
    maybePublish();
}
