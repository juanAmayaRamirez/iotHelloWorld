#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"

#define echoPin 27
#define trigPin 26
#define ledPin 25

WiFiClientSecure net;
PubSubClient client(net);
long duration, distance;

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected");
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to AWS IoT...");
    if (client.connect(thingName)) {
      Serial.println(" connected");
    } else {
      Serial.print(" failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledPin, OUTPUT);

  connectWiFi();

  net.setCACert(rootCA);
  net.setCertificate(clientCert);
  net.setPrivateKey(privateKey);

  client.setServer(mqttServer, mqttPort);
}

void loop() {
  if (!client.connected()) connectMQTT();
  client.loop();

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = duration / 58.2;

  digitalWrite(ledPin, distance > 10 ? HIGH : LOW);

  char payload[64];
  snprintf(payload, sizeof(payload), "{\"distance\":%ld,\"unit\":\"cm\"}", distance);

  Serial.print("Publishing: ");
  Serial.println(payload);
  client.publish(publishTopic, payload);

  delay(2000);
}
