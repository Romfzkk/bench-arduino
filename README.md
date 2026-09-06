# Bench

Put your ESP32 project on your phone without wiring a screen to it.

You name the variables you care about, the app reads that list and builds an
interface out of it. Toggles, sliders, gauges, live charts. The phone talks
straight to the board over your own WiFi, so there is no cloud account, no
broker, and nothing that stops working when someone else's server goes down.

```cpp
#include <WiFi.h>
#include <Bench.h>

Bench bench("Greenhouse");

float temperature = 0;
bool  relay = false;

void setup() {
  Serial.begin(115200);
  WiFi.begin("SSID", "PASSWORD");
  while (WiFi.status() != WL_CONNECTED) delay(300);

  bench.number("temp", "Temperature", &temperature).unit("C").range(0, 50).readOnly();
  bench.boolean("relay", "Relay", &relay);

  bench.begin();
}

void loop() {
  temperature = readSensor();
  bench.loop();
}
```

That is the whole thing. You never describe a variable twice.

**New to this? Start with [GETTING-STARTED.md](GETTING-STARTED.md).** It goes
from a bare board to a working panel in about ten minutes.

## Install

Not in the Library Manager yet, so grab
[Bench.zip from the releases page](../../releases/latest) and use
**Sketch > Include Library > Add .ZIP Library**.

You also need **WebSockets** by Markus Sattler and **ArduinoJson** v7, both of
which are in the Library Manager.

PlatformIO:

```ini
lib_deps =
  https://github.com/Romfzkk/bench-arduino.git
  links2004/WebSockets
  bblanchon/ArduinoJson@^7.0.0
```

## Examples

`Minimal` is one sensor and one relay, and builds on ESP32 and ESP8266. Start
there.

`Greenhouse` is four sensors, a PWM fan, a grow light and a momentary pump.
ESP32 only, because it uses `ledcAttach`.

Both are compiled against ESP32 core 3.3.7 and ESP8266 core 3.1.2 before every
release.

## Registering variables

```cpp
bench.number("key", "Label", &floatVar);
bench.number("key", "Label", &intVar);
bench.boolean("key", "Label", &boolVar);
```

The key is what saved panels bind to. Treat it like a database column: renaming
it breaks panels people have already built. The label is cosmetic, change it
whenever.

Options chain onto the registration:

```cpp
bench.number("fan", "Fan Speed", &fanSpeed).range(0, 255).precision(0);
bench.number("temp", "Temperature", &temp).unit("C").range(0, 50).readOnly();
bench.boolean("pump", "Pump", &pump).writeOnly();
```

| Option | What it does |
| --- | --- |
| `.unit("C")` | Shown beside the value |
| `.range(lo, hi)` | Bounds gauges and sliders, and clamps writes |
| `.precision(n)` | Decimal places |
| `.readOnly()` | App shows it, never writes it |
| `.writeOnly()` | App writes it, never shows it |
| `.label("...")` | Change the label after registration |

Direction is worth getting right. The app only offers widgets a channel can
actually drive, so a `.readOnly()` sensor never gets a slider put on it. That
removes the whole class of panels where a control looks live but writes into
nothing.

## Runtime

| Call | What it does |
| --- | --- |
| `bench.begin()` | Starts the server and the mDNS advert |
| `bench.loop()` | Pumps the server and publishes changes. Every pass |
| `bench.setHostname("rig")` | mDNS name without `.local`. Before `begin()` |
| `bench.log(...)` | A line in the app's terminal widget |
| `bench.warn(...)`, `bench.error(...)` | Same, coloured |
| `bench.hasClients()` | True while a phone is connected |
| `bench.setUpdateInterval(ms)` | Publish rate, default 100 |
| `bench.setRefreshInterval(ms)` | Full resend, default 2000 |

## Things that will bite you

**Never call `delay()` in `loop()`.** It blocks the socket and the panel goes
dead on the phone. Gate your sensor reads on `millis()` the way the examples
do. This is the single most common way to break it.

**Your board must be on 2.4GHz.** ESP32 and ESP8266 cannot see 5GHz networks.
If discovery never finds anything, check this before anything else.

**Registered variables have to be global.** The library stores pointers to
them, so a local goes out of scope and you get garbage.

Twenty four channels by default. Raise it with `-DBENCH_MAX_CHANNELS=48` if you
need more.

Only changed values go on the wire, plus a full resend every couple of seconds
so a phone that reconnects catches up.

## Discovery

`begin()` advertises the board as `_bench._tcp` and claims `bench.local`, so
the app finds it without anyone typing an IP.

Give each board its own name if you run several:

```cpp
bench.setHostname("greenhouse");   // greenhouse.local
```

If your router blocks multicast, which some mesh systems do, the app falls back
to scanning the subnet instead. The IP printed on boot always works as a last
resort.

## Protocol

Newline-delimited JSON over a WebSocket, port 81, path `/ws`. Small enough to
implement by hand if you would rather not use the library.

Board to app:

```json
{"t":"hello","id":"rig","name":"My Rig","proto":1,"channels":[
  {"k":"temp","n":"Temperature","type":"number","mode":"r","unit":"C","min":0,"max":50}
]}
{"t":"v","k":"temp","v":23.4}
{"t":"vs","d":{"temp":23.4,"relay":true}}
{"t":"log","m":"boot ok","lvl":"info"}
{"t":"pong","ts":1234}
```

App to board:

```json
{"t":"get"}
{"t":"set","k":"relay","v":true}
{"t":"ping","ts":1234}
```

`type` is `bool`, `number` or `text`. `mode` is `r`, `w` or `rw`. Anything the
app cannot parse is dropped rather than treated as an error, so a half
implemented board still works.

## Licence

MIT, see [LICENSE](LICENSE).
