// Self test. Proves the whole path works, in both directions.
//
// Nothing here needs any wiring. Flash it, open the app, tap Build it for me,
// and you get a panel that shows you exactly what is and is not working.
//
// What to look for:
//
//   Uptime      counts up every second. If it is frozen, the board is not
//               sending, or loop() is blocked somewhere.
//   Temperature drifts on its own. Turn Heater on and it climbs fast. That is
//               the round trip: your phone changed something, the board acted
//               on it, and the result came back.
//   Doubled     is always exactly twice the Dial slider. If you move the
//               slider and this follows, numeric writes work.
//   Presses     counts up each time you hold the Ping button.
//   Device Log  gets a line for every event.
//
// Builds on ESP32 and ESP8266.

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <Bench.h>

const char *WIFI_SSID = "YOUR_WIFI";
const char *WIFI_PASS = "YOUR_PASSWORD";

Bench bench("Self Test");

// Read only, so the app shows them and cannot change them.
float temperature = 20.0;
int uptime = 0;
int doubled = 0;
int presses = 0;

// Writable, so the app gets controls for them.
bool heater = false;
int dial = 0;
bool ping = false;

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Joining WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Left on the default bench.local on purpose. A custom hostname is invisible
  // to the app's hostname probe unless it happens to be on its list, and this
  // example exists to be found.

  bench.number("temp", "Temperature", &temperature).unit("C").range(15, 60).precision(1).readOnly();
  bench.number("uptime", "Uptime", &uptime).unit("s").readOnly();
  bench.number("doubled", "Doubled", &doubled).range(0, 200).readOnly();
  bench.number("presses", "Presses", &presses).readOnly();

  bench.boolean("heater", "Heater", &heater);
  bench.number("dial", "Dial", &dial).range(0, 100);
  bench.boolean("ping", "Ping", &ping).writeOnly();

  bench.begin();
  bench.log("self test ready");
}

void loop() {
  tick();
  bench.loop();
}

void tick() {
  static uint32_t lastTick = 0;
  if (millis() - lastTick < 200)
    return;
  lastTick = millis();

  // Heating and cooling, so the number is never static and reacts to the
  // Heater switch within a couple of seconds.
  float target = heater ? 55.0 : 20.0;
  temperature += (target - temperature) * 0.04;

  // Arithmetic the board does, not the app. If Doubled tracks the Dial slider
  // then a value really did travel phone to board and back.
  doubled = dial * 2;

  reportUptime();
  watchHeater();
  watchPing();
}

void reportUptime() {
  int seconds = millis() / 1000;
  if (seconds == uptime)
    return;

  uptime = seconds;

  // A heartbeat in the terminal, often enough to see, rare enough to read.
  if (uptime % 10 == 0)
    bench.log("up " + String(uptime) + "s, temp " + String(temperature, 1) + "C");
}

void watchHeater() {
  static bool wasOn = false;
  if (heater == wasOn)
    return;

  wasOn = heater;
  if (heater)
    bench.warn("heater on, temperature climbing");
  else
    bench.log("heater off, cooling back down");
}

void watchPing() {
  static bool wasPressed = false;
  if (ping == wasPressed)
    return;

  wasPressed = ping;
  if (!ping)
    return;

  presses++;
  bench.log("ping " + String(presses));
}
