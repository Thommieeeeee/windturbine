#include <WiFi.h>
#include <PubSubClient.h>
#include "HX711.h"
#include <WiFiUdp.h>
#include <NTPClient.h>

// Configure Wifi
const char* ssid = "eduroam";
const char* username = "bzwzw@edu.nl";
const char* password = "bvwcg";

// Configure MQTT (ThingsBoard)
const char* mqtt_server = "mqtt.eu.thingsboard.cloud";
const int mqtt_port = 1883;
const char* access_token = "L6MgZuzAtW6W7iDfgwdf";

// HX711 pins
#define DOUT 26
#define CLK 25

HX711 scale;

WiFiClient espClient;
PubSubClient client(espClient);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // UTC time, update every 60 seconds

unsigned long lastMsg = 0;
const long interval = 500;

// Variables for calibration
long zeroOffset = 0;
long maxTensionOffset = 0;
long maxCompressionOffset = 0;  

// Setup for Wifi
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

// Function for MQTT connection
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT server...");
    if (client.connect("ESP32", access_token, NULL)) {
      Serial.println("MQTT connected!");
    } else {
      Serial.print("Errorcode: ");
      Serial.print(client.state());
      Serial.println(" — trying again in 5s");
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
  scale.set_scale();
  scale.tare();

  // Automatic calibration using serial input
  Serial.println("Wait for calibration commands:");
  Serial.println("Put the ruler in unbent position and press 'enter'");
  while (Serial.available() == 0) {}
  Serial.read();  // emptying buffer
  zeroOffset = scale.read_average(10);
  Serial.print("Neutral position (zeroOffset): ");
  Serial.println(zeroOffset);

  Serial.println("Bend the ruler with the ends downwards and send a 'b'");
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'b') {
        maxTensionOffset = scale.read_average(10);
        Serial.print("Maximum tension: ");
        Serial.println(maxTensionOffset);
        break;
      }
    }
  }

  Serial.println("Bend the ruler with the ends upwards and send an 'o'");
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'o') {
        maxCompressionOffset = scale.read_average(10);
        Serial.print("Maximum compression: ");
        Serial.println(maxCompressionOffset);
        break;
      }
    }
  }

  Serial.println("HX711 ready!");

  timeClient.begin();
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  Serial.print("Current time (epoch): ");
  Serial.println(timeClient.getEpochTime());
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > interval) {
    lastMsg = now;

    // Read raw value
    long raw = scale.read();
    Serial.print("Raw value: ");
    Serial.println(raw);

    float strain_percent = 0;

    // Calculate strain
    if (raw >= zeroOffset) {
      strain_percent = ((float)(raw - zeroOffset) / (maxTensionOffset - zeroOffset)) * 100.0;
    } else {
      strain_percent = ((float)(raw - zeroOffset) / (zeroOffset - maxCompressionOffset)) * 100.0;
    }

    strain_percent = constrain(strain_percent, -100.0, 100.0);  // Clamp till ±100%

    Serial.print("Strain (%): ");
    Serial.println(strain_percent, 2);

    // JSON payload with strain
    String payload = "{\"ts\":";
    payload += String(timeClient.getEpochTime());
    payload += ", \"s\":";  // s = strain
    payload += String(strain_percent, 2);
    payload += "}";

    Serial.print("MQTT payload: ");
    Serial.println(payload);

    // Send payload to dashboard
    client.publish("v1/devices/me/telemetry", (char*)payload.c_str());
  }

}