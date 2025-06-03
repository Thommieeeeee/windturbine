#include <WiFi.h>
#include <PubSubClient.h>
#include "HX711.h"

// === WIFI ===
const char* ssid = "eduroam";
const char* username = "bzwzw@edu.nl";
const char* password = "bvwcg";

// === MQTT (ThingsBoard) ===
const char* mqtt_server = "mqtt.eu.thingsboard.cloud";
const int mqtt_port = 1883;
const char* access_token = "L6MgZuzAtW6W7iDfgwdf";

// HX711 pins
#define DOUT 26
#define CLK 25

HX711 scale;

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMsg = 0;
const long interval = 5000;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi");

  WiFi.begin(ssid, WPA2_AUTH_PEAP, username, username, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.print("\nWiFi connected, status: ");
  Serial.println(WiFi.status());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Verbinding maken met MQTT server...");
    if (client.connect("ESP32", access_token, NULL)) {
      Serial.println("MQTT verbonden!");
    } else {
      Serial.print("Foutcode: ");
      Serial.print(client.state());
      Serial.println(" — opnieuw proberen in 5s");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);

  // HX711 setup
  scale.begin(DOUT, CLK);
  scale.set_scale(161.215);  // Kalibratie
  scale.tare();
  Serial.println("HX711 klaar!");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > interval) {
    lastMsg = now;

    long raw = scale.read();
    Serial.print("Raw waarde: ");
    Serial.println(raw);

    float gewicht = scale.get_units(10);  // Gemiddelde over 10 metingen
    Serial.print("Gewicht: ");
    Serial.println(gewicht);

    // JSON payload opbouwen
    String payload = "{\"g\":";
    payload += String(gewicht, 2);
    payload += "}";

    Serial.print("MQTT payload: ");
    Serial.println(payload);

    // Stuur naar ThingsBoard
    client.publish("v1/devices/me/telemetry", (char*)payload.c_str());
  }
}