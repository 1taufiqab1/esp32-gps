#pragma once

#include <pgmspace.h>

// WiFi Credentials
#define WIFI_SSID "xxx"
#define WIFI_PASSWORD "xxx"

// MQTT Broker & Playit Tunnel Config
#define MQTT_HOST "mqtt.domain.com"
#define MQTT_PORT 12345
#define MQTT_USERNAME "mqtt"
#define MQTT_PASSWORD "xxx"

// Let's Encrypt Root CA (ISRG Root X1)
static const char MQTT_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
xxxx
xxxx
xxxx
xxxx
-----END CERTIFICATE-----
)EOF";
