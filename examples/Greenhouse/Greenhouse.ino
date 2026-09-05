/*
  Bench — Greenhouse example

  Mirrors the demo panel in the Bench app: four sensors, a PWM fan, a grow
  light and a momentary pump trigger.

  Wiring here is stubbed with fake readings so the sketch runs on a bare
  board. Replace readTemperature() and friends with your real sensors.

  1. Set WIFI_SSID and WIFI_PASSWORD below.
  2. Flash, then open Serial Monitor at 115200 and note the printed IP.
  3. In Bench: Boards -> Add board -> that IP, port 81, path /ws.
*/

#include <WiFi.h>
#include <Bench.h>

const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

Bench bench("Greenhouse");

// Every variable you register must outlive the sketch, so keep them global.
float temperature = 24.0f;
float humidity = 60.0f;
int lightLevel = 240;
float soilMoisture = 47.0f;

int fanSpeed = 0;      // 0-255 PWM
bool growLight = false;
bool waterPump = false;
bool doorOpen = false;

const int PIN_FAN = 25;
const int PIN_GROW = 26;
const int PIN_PUMP = 27;
const int PIN_DOOR = 32;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_GROW, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_DOOR, INPUT_PULLUP);
  ledcAttach(PIN_FAN, 5000, 8);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(300);
  }
  Serial.println();
  Serial.print("Bench address: ");
  Serial.println(WiFi.localIP());  // <- type this into the app

  // Sensors the app can show but never write.
  bench.number("temp", "Temperature", &temperature)
      .unit("C").range(10, 45).precision(1).readOnly();

  bench.number("humid", "Humidity", &humidity)
      .unit("%").range(0, 100).precision(0).readOnly();

  bench.number("lux", "Light Level", &lightLevel)
      .unit("lx").range(0, 2000).precision(0).readOnly();

  bench.number("soil", "Soil Moisture", &soilMoisture)
      .unit("%").range(0, 100).precision(0).readOnly();

  // Outputs the app can drive.
  bench.number("fan", "Fan Speed", &fanSpeed).range(0, 255).precision(0);
  bench.boolean("grow", "Grow Light", &growLight);
  bench.boolean("pump", "Water Pump", &waterPump).writeOnly();

  // A read-only flag drives an indicator widget.
  bench.boolean("door", "Door Open", &doorOpen).readOnly();

  bench.begin();
  bench.log("boot ok");
}

void loop() {
  readSensors();
  applyOutputs();

  // Bench only puts changed values on the wire, so calling this every pass is
  // cheap. Do not add a delay() here or the panel will feel laggy.
  bench.loop();
}

void readSensors() {
  static uint32_t last = 0;
  if (millis() - last < 200) return;
  last = millis();

  // Replace these with real sensor reads.
  temperature += (24.0f + (growLight ? 2.4f : 0.0f) - (fanSpeed / 255.0f) * 3.2f - temperature) * 0.06f;
  humidity += (62.0f - (fanSpeed / 255.0f) * 14.0f - humidity) * 0.04f;
  lightLevel += (int)(((growLight ? 1650 : 220) - lightLevel) * 0.12f);
  soilMoisture -= 0.004f;
  if (soilMoisture < 0) soilMoisture = 0;

  const bool door = digitalRead(PIN_DOOR) == LOW;
  if (door != doorOpen) {
    doorOpen = door;
    bench.warn(door ? "door opened" : "door closed");
  }

  if (soilMoisture < 25.0f) {
    static uint32_t warned = 0;
    if (millis() - warned > 15000) {
      warned = millis();
      bench.error("soil low: " + String((int)soilMoisture) + "%");
    }
  }
}

void applyOutputs() {
  ledcWrite(PIN_FAN, fanSpeed);
  digitalWrite(PIN_GROW, growLight ? HIGH : LOW);

  // waterPump is momentary: the app sends true on press, false on release.
  digitalWrite(PIN_PUMP, waterPump ? HIGH : LOW);
  if (waterPump) soilMoisture = min(100.0f, soilMoisture + 0.05f);
}
