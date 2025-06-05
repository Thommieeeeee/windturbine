#include <WiFi.h>
#include <PubSubClient.h>
#include "HX711.h"
#include <WiFiUdp.h>
#include <NTPClient.h>

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

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // UTC tijd, update elke 60 sec

unsigned long lastMsg = 0;
const long interval_measure = 500;
const long interval_send = 2000;

String payloadBuffer = "";

long zeroOffset = 0;            // Kalibreer deze waarde bij initialisatie
long maxTensionOffset = 0;      // maximale buiging (positieve rek)
long maxCompressionOffset = 0;  // maximale bolling (negatieve rek)

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
  scale.set_scale();
  scale.tare();

  // === Automatische kalibratie via seriële invoer ===
  Serial.println("Wacht op kalibratiecommando's:");
  Serial.println("Plaats liniaal in neutrale (rechte) toestand en druk op ENTER");
  while (Serial.available() == 0) {}
  Serial.read();  // leegmaken buffer
  zeroOffset = scale.read_average(10);
  Serial.print("Neutrale stand (zeroOffset): ");
  Serial.println(zeroOffset);

  Serial.println("Plaats liniaal in maximale buiging en druk op 'b'");
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'b') {
        maxTensionOffset = scale.read_average(10);
        Serial.print("Max buiging (tension): ");
        Serial.println(maxTensionOffset);
        break;
      }
    }
  }

  Serial.println("Plaats liniaal in maximale bolling en druk op 'o'");
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'o') {
        maxCompressionOffset = scale.read_average(10);
        Serial.print("Max bolling (compressie): ");
        Serial.println(maxCompressionOffset);
        break;
      }
    }
  }

  Serial.println("HX711 klaar!");

  timeClient.begin();
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  Serial.print("Huidige tijd (epoch): ");
  Serial.println(timeClient.getEpochTime());
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > interval_measure) {
    lastMsg = now;

    long raw = scale.read();
    Serial.print("Raw waarde: ");
    Serial.println(raw);

    float strain_percent = 0;

    if (raw >= zeroOffset) {
      strain_percent = ((float)(raw - zeroOffset) / (maxTensionOffset - zeroOffset)) * 100.0;
    } else {
      strain_percent = ((float)(raw - zeroOffset) / (zeroOffset - maxCompressionOffset)) * 100.0;
    }

    strain_percent = constrain(strain_percent, -100.0, 100.0);  // Clamp tot ±100%


    Serial.print("Rek (%): ");
    Serial.println(strain_percent, 2);

    // JSON payload met rek
    String payload = "{\"ts\":";
    payload += String(timeClient.getEpochTime());
    payload += ", \"s\":";  // s = strain
    payload += String(strain_percent, 2);
    payload += "}";

    payloadBuffer += payload;
  }

  if (now - lastMsg > interval_send){
    String payload = "[" + payloadBuffer + "]";
    
    Serial.print("MQTT payload: ");
    Serial.println(payload);

    client.publish("v1/devices/me/telemetry", payload.c_str());
  }
}