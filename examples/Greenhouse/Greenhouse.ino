// Greenhouse controller: four sensors, a PWM fan, a grow light and a pump.
//
// ESP32 only, because of ledcAttach. See the Minimal example for a sketch
// that builds on ESP8266 too.
//
// Sensor reads here are faked so the sketch runs on a bare board. Swap
// readSensors() for your real hardware.
//
// Set WIFI_SSID and WIFI_PASS, flash, then open Bench and tap Find my board.

#include <WiFi.h>
#include <Bench.h>

const char *WIFI_SSID = "YOUR_WIFI";
const char *WIFI_PASS = "YOUR_PASSWORD";

const int PIN_FAN = 25;
const int PIN_GROW = 26;
const int PIN_PUMP = 27;
const int PIN_DOOR = 32;

Bench bench("Greenhouse");

// Registered variables must outlive the sketch, so they live here.
float temperature = 24.0;
float humidity = 60.0;
int lightLevel = 240;
float soilMoisture = 47.0;

int fanSpeed = 0;
bool growLight = false;
bool waterPump = false;
bool doorOpen = false;

void readSensors();
void applyOutputs();

void setup() {
  Serial.begin(115200);

  pinMode(PIN_GROW, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_DOOR, INPUT_PULLUP);
  ledcAttach(PIN_FAN, 5000, 8);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  bench.setHostname("greenhouse");

  bench.number("temp", "Temperature", &temperature).unit("C").range(10, 45).precision(1).readOnly();
  bench.number("humid", "Humidity", &humidity).unit("%").range(0, 100).precision(0).readOnly();
  bench.number("lux", "Light Level", &lightLevel).unit("lx").range(0, 2000).readOnly();
  bench.number("soil", "Soil Moisture", &soilMoisture).unit("%").range(0, 100).precision(0).readOnly();

  bench.number("fan", "Fan Speed", &fanSpeed).range(0, 255);
  bench.boolean("grow", "Grow Light", &growLight);
  bench.boolean("pump", "Water Pump", &waterPump).writeOnly();
  bench.boolean("door", "Door Open", &doorOpen).readOnly();

  bench.begin();
  bench.log("boot ok");
}

void loop() {
  readSensors();
  applyOutputs();

  // No delay() anywhere in loop(). Blocking here stalls the socket and the
  // panel goes dead on the phone.
  bench.loop();
}

void readSensors() {
  static uint32_t last = 0;
  if (millis() - last < 200)
    return;
  last = millis();

  // Replace with real sensor reads. The fake model is coupled on purpose:
  // the fan cools, the grow light heats.
  float target = 24.0 + (growLight ? 2.4 : 0.0) - (fanSpeed / 255.0) * 3.2;
  temperature += (target - temperature) * 0.06;
  humidity += (62.0 - (fanSpeed / 255.0) * 14.0 - humidity) * 0.04;
  lightLevel += ((growLight ? 1650 : 220) - lightLevel) * 0.12;

  soilMoisture -= 0.004;
  if (soilMoisture < 0)
    soilMoisture = 0;

  bool door = digitalRead(PIN_DOOR) == LOW;
  if (door != doorOpen) {
    doorOpen = door;
    bench.warn(door ? "door opened" : "door closed");
  }

  if (soilMoisture < 25.0) {
    static uint32_t warned = 0;
    if (millis() - warned > 15000) {
      warned = millis();
      bench.error("soil low: " + String((int)soilMoisture) + "%");
    }
  }
}

void applyOutputs() {
  ledcWrite(PIN_FAN, fanSpeed);
  digitalWrite(PIN_GROW, growLight);

  // The app sends true on press and false on release for a momentary button.
  digitalWrite(PIN_PUMP, waterPump);
  if (waterPump && soilMoisture < 100.0)
    soilMoisture += 0.05;
}
