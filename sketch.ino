#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>
#include <ArduinoJson.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "secrets.h"

#define ESP32_RX2_PIN 16
#define ESP32_TX2_PIN 17
#define GPS_BAUD 9600
#define LED_A_PIN 32
#define LED_B_PIN 33
#define DEVICE_ID "esp32:001"

const char* wifi_ssid = WIFI_SSID;
const char* wifi_password = WIFI_PASSWORD;

const char* mqtt_server = MQTT_HOST;
const uint16_t mqtt_port = MQTT_PORT;
const char* mqtt_username = MQTT_USERNAME;
const char* mqtt_password = MQTT_PASSWORD;
const char* mqtt_root_ca = MQTT_ROOT_CA;

const char* mqtt_topic = "tracker/devices/" DEVICE_ID "/telemetry";
const char* mqtt_command_topic = "tracker/devices/" DEVICE_ID "/command";

const uint16_t mqtt_keep_alive = 60;
const size_t msg_size = 256;
const size_t cmd_size = 192;

const unsigned long wifi_retry_ms = 10000;
const unsigned long mqtt_retry_ms = 5000;
const unsigned long blink_fast_ms = 150;
const unsigned long blink_slow_ms = 500;
const unsigned long blip_ms = 100;

HardwareSerial gps_serial(2);
TinyGPSPlus gps;
WiFiClientSecure mqtt_client;
PubSubClient mqtt(mqtt_client);

unsigned long last_send = 0;
unsigned long last_publish_ok = 0;
unsigned long last_wifi_attempt = 0;
unsigned long last_mqtt_attempt = 0;
unsigned long last_led_a_toggle = 0;
unsigned long last_led_b_toggle = 0;

uint32_t last_command_timestamp = 0;

bool wifi_connected_before = false;
bool mqtt_connected_before = false;
bool gps_enabled = false;
bool led_a_state = false;
bool led_b_state = false;

int interval_seconds = 10;
int last_gps_fix_state = -1;

void on_message(char* topic, byte* payload, unsigned int length);

int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned month_prime = (unsigned)((int)month + (month > 2 ? -3 : 9));
    const unsigned day_of_year = (153 * month_prime + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;

    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

uint32_t gps_epoch() {
    if (!gps.date.isValid() || !gps.time.isValid()) {
        return 0;
    }

    int64_t timestamp =
        days_from_civil(gps.date.year(), gps.date.month(), gps.date.day()) * 86400LL +
        gps.time.hour() * 3600LL +
        gps.time.minute() * 60LL +
        gps.time.second();

    if (timestamp <= 0 || timestamp > UINT32_MAX) {
        return 0;
    }

    return (uint32_t)timestamp;
}

bool get_unix_timestamp(uint32_t &timestamp) {
    time_t now = time(nullptr);

    if (now >= 1700000000 && (uint64_t)now <= UINT32_MAX) {
        timestamp = (uint32_t)now;
        return true;
    }

    uint32_t gps_timestamp = gps_epoch();

    if (gps_timestamp >= 1700000000) {
        timestamp = gps_timestamp;
        return true;
    }

    return false;
}

void initialize_hardware() {
    Serial.begin(9600);
    pinMode(LED_A_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
    digitalWrite(LED_A_PIN, LOW);
    digitalWrite(LED_B_PIN, LOW);

    gps_serial.setRxBufferSize(2048);
    gps_serial.begin(GPS_BAUD, SERIAL_8N1, ESP32_RX2_PIN, ESP32_TX2_PIN);
}

void initialize_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    Serial.println("[WIFI] Connecting...");
    WiFi.begin(wifi_ssid, wifi_password);
    last_wifi_attempt = millis();
}

void maintain_wifi() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifi_connected_before) {
            wifi_connected_before = true;
            Serial.printf("[WIFI] Connected | IP=%s\n", WiFi.localIP().toString().c_str());

            configTime(0, 0, "pool.ntp.org", "time.google.com");
            last_mqtt_attempt = millis() - mqtt_retry_ms;
        }
        return;
    }

    if (wifi_connected_before) {
        wifi_connected_before = false;
        Serial.println("[WIFI] Disconnected | Reconnecting...");

        if (mqtt.connected()) {
            mqtt.disconnect();
        }

        mqtt_connected_before = false;
        last_wifi_attempt = millis() - wifi_retry_ms;
    }

    unsigned long now = millis();

    if (now - last_wifi_attempt >= wifi_retry_ms) {
        last_wifi_attempt = now;
        Serial.println("[WIFI] Reconnecting...");
        WiFi.reconnect();
    }
}

void initialize_mqtt() {
    mqtt_client.setCACert(mqtt_root_ca);
    mqtt_client.setHandshakeTimeout(10);

    mqtt.setServer(mqtt_server, mqtt_port);
    mqtt.setKeepAlive(mqtt_keep_alive);
    mqtt.setSocketTimeout(5);
    mqtt.setBufferSize(512);
    mqtt.setCallback(on_message);

    Serial.printf("[MQTT] TLS Broker | %s:%u\n", mqtt_server, mqtt_port);
}

void reconnect_mqtt() {
    if (mqtt.connected()) {
        if (!mqtt_connected_before) {
            mqtt_connected_before = true;
            Serial.println("[MQTT] Connected (TLS)");
        }
        return;
    }

    if (mqtt_connected_before) {
        mqtt_connected_before = false;
        Serial.println("[MQTT] Disconnected | Reconnecting...");
    }

    if (millis() - last_mqtt_attempt < mqtt_retry_ms) {
        return;
    }

    last_mqtt_attempt = millis();

    if (time(nullptr) < 1700000000) {
        Serial.println("[TLS] Waiting for NTP time sync...");
        return;
    }

    Serial.println("[MQTT] Reconnecting (TLS)...");

    if (mqtt.connect(DEVICE_ID, mqtt_username, mqtt_password)) {
        if (mqtt.subscribe(mqtt_command_topic)) {
            mqtt_connected_before = true;
            Serial.println("[MQTT] Connected (TLS) | Command subscribed");
        } else {
            Serial.println("[MQTT] Connected (TLS) | Subscribe failed");
            mqtt.disconnect();
        }
    } else {
        Serial.printf("[MQTT] Connect failed | State=%d\n", mqtt.state());

        char tls_err[128];
        if (mqtt_client.lastError(tls_err, sizeof(tls_err)) < 0) {
            Serial.printf("[TLS] Error | %s\n", tls_err);
        }
    }
}

void discard_gps_data() {
    while (gps_serial.available() > 0) {
        gps_serial.read();
    }
}

void set_active(bool enable) {
    if (gps_enabled == enable) {
        return;
    }

    gps_enabled = enable;

    if (enable) {
        gps = TinyGPSPlus();
        last_gps_fix_state = -1;
        Serial.println("[GPS] Enabled | Waiting for fix");
    } else {
        Serial.println("[GPS] Disabled");
    }
}

bool is_valid_interval(int interval) {
    return interval == 0 || interval == 2 || interval == 5 || interval == 10 || interval == 30;
}

void reject_command(const char* reason) {
    Serial.printf("[CMD] Rejected | %s\n", reason);
}

void handle_command(char* command) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, command);

    if (error) {
        reject_command("Invalid JSON");
        return;
    }

    JsonVariantConst device_value = doc["device_id"];
    JsonVariantConst active_value = doc["active"];
    JsonVariantConst timestamp_value = doc["timestamp"];

    if (!device_value.is<const char*>()) {
        reject_command("Missing or invalid device_id");
        return;
    }

    if (!active_value.is<bool>()) {
        reject_command("Missing or invalid active");
        return;
    }

    if (!timestamp_value.is<uint32_t>()) {
        reject_command("Missing or invalid timestamp");
        return;
    }

    const char* command_device_id = device_value.as<const char*>();
    bool requested_active = active_value.as<bool>();
    uint32_t command_timestamp = timestamp_value.as<uint32_t>();

    Serial.printf("[CMD] Received | device=%s active=%s\n", command_device_id, requested_active ? "true" : "false");

    if (strcmp(command_device_id, DEVICE_ID) != 0) {
        reject_command("Device ID mismatch");
        return;
    }

    if (command_timestamp == 0) {
        reject_command("Invalid timestamp");
        return;
    }

    if (last_command_timestamp != 0 && command_timestamp < last_command_timestamp) {
        reject_command("Older timestamp");
        return;
    }

    int next_interval = interval_seconds;

    if (doc.containsKey("interval_seconds")) {
        JsonVariantConst interval_value = doc["interval_seconds"];

        if (!interval_value.is<int>()) {
            reject_command("Invalid interval_seconds");
            return;
        }

        next_interval = interval_value.as<int>();

        if (!is_valid_interval(next_interval)) {
            reject_command("Unsupported interval");
            return;
        }
    } else if (requested_active) {
        next_interval = 10;
    }

    if (requested_active && next_interval == 0) {
        next_interval = 10;
    }

    last_command_timestamp = command_timestamp;
    interval_seconds = next_interval;
    set_active(requested_active);
    last_send = millis();

    Serial.printf("[CMD] Accepted | GPS=%s interval=%ds\n", requested_active ? "ON" : "OFF", interval_seconds);
}

void on_message(char* topic, byte* payload, unsigned int length) {
    if (length >= cmd_size) {
        reject_command("Payload too large");
        return;
    }

    char command_buffer[cmd_size];
    memcpy(command_buffer, payload, length);
    command_buffer[length] = '\0';

    handle_command(command_buffer);
}

void read_gps_data() {
    while (gps_serial.available() > 0) {
        gps.encode(gps_serial.read());
    }
}

void update_gps_status() {
    int current_fix = gps.location.isValid() ? 1 : 0;

    if (current_fix == last_gps_fix_state) {
        return;
    }

    last_gps_fix_state = current_fix;

    uint32_t satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;

    if (current_fix) {
        Serial.printf("[GPS] Fix OK | Satellites=%lu\n", (unsigned long)satellites);
    } else {
        Serial.printf("[GPS] No fix | Satellites=%lu\n", (unsigned long)satellites);
    }
}

bool is_telemetry_interval_elapsed() {
    if (!gps_enabled || interval_seconds <= 0) {
        return false;
    }

    unsigned long interval_ms = (unsigned long)interval_seconds * 1000UL;

    if (millis() - last_send >= interval_ms) {
        last_send = millis();
        return true;
    }

    return false;
}

void publish_telemetry() {
    if (!gps.location.isValid()) {
        Serial.println("[TX] Skipped | GPS fix unavailable");
        return;
    }

    uint32_t timestamp;

    if (!get_unix_timestamp(timestamp)) {
        Serial.println("[TX] Skipped | Timestamp unavailable");
        return;
    }

    double latitude = gps.location.lat();
    double longitude = gps.location.lng();
    double speed = gps.speed.kmph();
    uint32_t satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;

    StaticJsonDocument<256> doc;
    doc["device_id"] = DEVICE_ID;
    doc["lat"] = latitude;
    doc["lng"] = longitude;
    doc["speed"] = speed;
    doc["satellites"] = satellites;
    doc["timestamp"] = timestamp;

    char payload[msg_size];

    size_t payload_length = serializeJson(doc, payload, sizeof(payload));

    if (payload_length == 0 || payload_length >= sizeof(payload)) {
        Serial.println("[TX] Failed | Payload error");
        return;
    }

    if (mqtt.publish(mqtt_topic, payload)) {
        last_publish_ok = millis();

        Serial.printf(
            "[TX] Publishing | DEVICE=%s LAT=%.6f LON=%.6f SPEED=%.1fkm/h SAT=%lu TIMESTAMP=%lu | Success\n",
            DEVICE_ID,
            latitude,
            longitude,
            speed,
            (unsigned long)satellites,
            (unsigned long)timestamp
        );
    }
}

void process_gps() {
    if (!gps_enabled) {
        discard_gps_data();
        return;
    }

    read_gps_data();
    update_gps_status();

    if (!is_telemetry_interval_elapsed()) {
        return;
    }

    last_send = millis();

    uint32_t satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;

    if (!gps.location.isValid()) {
        Serial.printf("[GPS] Waiting for fix | SAT=%lu\n", (unsigned long)satellites);
        return;
    }

    publish_telemetry();
}

void update_leds() {
    unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        if (now - last_led_a_toggle >= blink_fast_ms) {
            last_led_a_toggle = now;
            led_a_state = !led_a_state;
            digitalWrite(LED_A_PIN, led_a_state);
        }
    } else if (!mqtt.connected()) {
        if (now - last_led_a_toggle >= blink_slow_ms) {
            last_led_a_toggle = now;
            led_a_state = !led_a_state;
            digitalWrite(LED_A_PIN, led_a_state);
        }
    } else {
        led_a_state = true;
        digitalWrite(LED_A_PIN, HIGH);
    }

    if (!gps_enabled) {
        led_b_state = false;
        digitalWrite(LED_B_PIN, LOW);
    } else if (!gps.location.isValid()) {
        if (now - last_led_b_toggle >= blink_slow_ms) {
            last_led_b_toggle = now;
            led_b_state = !led_b_state;
            digitalWrite(LED_B_PIN, led_b_state);
        }
    } else {
        led_b_state = !(last_publish_ok != 0 && now - last_publish_ok < blip_ms);
        digitalWrite(LED_B_PIN, led_b_state);
    }
}

void setup() {
    initialize_hardware();
    initialize_wifi();
    initialize_mqtt();
}

void loop() {
    process_gps();
    maintain_wifi();

    if (WiFi.status() == WL_CONNECTED) {
        reconnect_mqtt();

        if (mqtt.connected()) {
            mqtt.loop();
        }
    } else if (mqtt.connected()) {
        mqtt.disconnect();
    }

    process_gps();
    update_leds();
}
