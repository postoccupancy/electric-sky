#include <Arduino.h>

#define LED_PIN 2

void setup() {
  delay(3000);
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  Serial.println();
  Serial.println("Electric Sky firmware booted");
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED on");
  delay(500);

  digitalWrite(LED_PIN, LOW);
  Serial.println("LED off");
  delay(500);
}