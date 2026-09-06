# Changelog

## 1.0.1

Fixes three build errors that were in 1.0.0. They only turned up once the
library was actually compiled rather than just read.

- `sendLine` took a `const String&`, but `WebSocketsServer::sendTXT` and
  `broadcastTXT` both want a non-const `String&`, so it would not bind
- `String(float, uint8_t)` is ambiguous against the integer constructors on
  ESP32 and needs an explicit cast on the precision argument
- the Minimal example used `LED_BUILTIN`, which is not defined for the generic
  esp32 board

Added:

- `SelfTest` example, which needs no wiring and proves the round trip in both
  directions
- `Minimal` example that builds on ESP32 and ESP8266
- GitHub Actions compiling every example on both platforms, so this class of
  mistake cannot ship again

## 1.0.0

First release. Never worked; use 1.0.1.
