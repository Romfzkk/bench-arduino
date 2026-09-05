# Bench for Arduino

Expose variables from an ESP32 or ESP8266 to the [Bench](https://play.google.com/store)
mobile app. No cloud, no broker, no account. The phone opens a WebSocket
straight to your board on your own network.

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

That is the whole integration. The app asks the board what it exposes and
builds the interface from the answer, so you never describe a variable twice.

## Install

Not in the Arduino Library Manager yet, so it installs from a ZIP:

1. Download `Bench.zip` from [Releases](../../releases/latest)
2. Arduino IDE: **Sketch** > **Include Library** > **Add .ZIP Library**

Then install these two from **Manage Libraries**:

- **WebSockets** by Markus Sattler
- **ArduinoJson** v7 by Benoit Blanchon

PlatformIO:

```ini
lib_deps =
  https://github.com/romfzkk/bench-arduino.git
  links2004/WebSockets
  bblanchon/ArduinoJson@^7.0.0
```

## API

### Registering variables

| Call | Purpose |
| --- | --- |
| `bench.number(key, label, &floatVar)` | Numeric channel backed by a `float` |
| `bench.number(key, label, &intVar)` | Numeric channel backed by an `int` |
| `bench.boolean(key, label, &boolVar)` | On/off channel backed by a `bool` |

`key` is the stable identifier the app stores in saved panels. Renaming it
breaks existing panels, so treat it like a database column. `label` is cosmetic
and safe to change whenever.

### Options, chained onto the registration

| Option | Effect |
| --- | --- |
| `.unit("C")` | Unit shown beside the value |
| `.range(lo, hi)` | Bounds for gauges and sliders, and clamping on write |
| `.precision(n)` | Decimal places to display |
| `.readOnly()` | App may display it, never write it |
| `.writeOnly()` | App may write it, never displays it |
| `.label("...")` | Override the label after registration |

Direction matters. The app only offers widgets a channel can actually drive, so
a `.readOnly()` channel never gets a slider and a `.writeOnly()` channel never
gets a gauge. That removes the whole class of dashboards where a control looks
live but writes nowhere.

### Runtime

| Call | Purpose |
| --- | --- |
| `bench.begin()` | Start the server and advertise over mDNS |
| `bench.loop()` | Pump the server and publish changes. Call every pass |
| `bench.setHostname("rig")` | mDNS name without `.local`. Call before `begin()` |
| `bench.log("...")` | Line in the app's terminal widget |
| `bench.warn(...)` / `bench.error(...)` | Same, coloured by severity |
| `bench.hasClients()` | True while a phone is connected |
| `bench.setUpdateInterval(ms)` | Publish cadence, default 100 ms |
| `bench.setRefreshInterval(ms)` | Full state resend, default 2000 ms |

## Discovery

`begin()` advertises the board as `_bench._tcp` and claims `bench.local`, so
the app finds it without anyone typing an IP address.

Three things have to line up:

- phone and board on the **same network**, not a guest or IoT VLAN
- the router must not block multicast, which some mesh systems do
- one board per hostname, so give each a distinct `setHostname()`

When discovery is blocked the app falls back to sweeping the subnet, which
needs no multicast. The IP printed on boot always works as a last resort.

## Notes

- **Never call `delay()` in `loop()`.** Gate sensor reads on `millis()` instead,
  the way the example does. A blocking delay stalls the socket and the panel
  goes dead.
- Only changed values go on the wire, plus a full refresh every couple of
  seconds so charts stay continuous after a reconnect.
- Registered variables must be global, or otherwise outlive the sketch. The
  library stores pointers to them.
- Default limit is 24 channels. Raise it with `-DBENCH_MAX_CHANNELS=48`.
- On ESP8266 the mDNS responder is pumped from `bench.loop()`. On ESP32 it runs
  on its own task. Either way `bench.loop()` every pass is required.
- Reflashing reboots the board. The app reconnects on its own with backoff, so
  leave the panel open while you iterate.

## Protocol

If you would rather not use the library, the wire format is small enough to
implement by hand. Newline-delimited JSON over a WebSocket, default port 81,
path `/ws`.

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
app cannot parse is discarded rather than treated as an error, so a partially
implemented board still works.

## Licence

MIT. See [LICENSE](LICENSE).
