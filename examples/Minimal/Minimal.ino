// Smallest useful Bench sketch. Builds on both ESP32 and ESP8266.
//
// One sensor the app can read, one output it can switch. Set the two WiFi
// constants, flash, then open Bench and tap Find my board.

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <Bench.h>

const char *WIFI_SSID = "YOUR_WIFI";
const char *WIFI_PASS = "YOUR_PASSWORD";

// LED_BUILTIN is not defined for every ESP32 variant, so name the pin.
// GPIO 2 is the onboard LED on most ESP32 and NodeMCU boards.
const int PIN_RELAY = 2;

Bench bench("My Rig");

float temperature = 0;
bool relay = false;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY, OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  bench.number("temp", "Temperature", &temperature).unit("C").range(0, 50).precision(1).readOnly();
  bench.boolean("relay", "Relay", &relay);

  bench.begin();
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 250) {
    last = millis();
    // Replace with a real sensor read.
    temperature = 20.0 + (millis() % 10000) / 1000.0;
  }

  digitalWrite(PIN_RELAY, relay);

  bench.loop();
}
