// Bench - phone control panel for ESP32 / ESP8266 projects.
// https://github.com/Romfzkk/bench-arduino
//
// Copyright (c) 2026 romfzk. MIT licence, see LICENSE.
//
// Depends on WebSockets (Links2004) and ArduinoJson 7.

#ifndef BENCH_H
#define BENCH_H

#include <Arduino.h>

#ifndef BENCH_MAX_CHANNELS
#define BENCH_MAX_CHANNELS 24
#endif

#define BENCH_VERSION "1.0.0"
#define BENCH_PROTOCOL 1

class WebSocketsServer;

enum class BenchType : uint8_t { Bool, Int, Float };
enum class BenchMode : uint8_t { Read, Write, ReadWrite };

// One variable published to the app. number() and boolean() return a reference
// so options can be chained onto the registration.
class BenchChannel {
public:
  BenchChannel &unit(const char *text);
  BenchChannel &range(float low, float high);
  BenchChannel &precision(uint8_t places);
  BenchChannel &label(const char *text);

  // Direction decides which widgets the app is willing to offer, so a sensor
  // never ends up behind a slider.
  BenchChannel &readOnly();
  BenchChannel &writeOnly();

private:
  friend class Bench;

  const char *_key = nullptr;
  const char *_name = nullptr;
  const char *_unit = nullptr;
  void *_ptr = nullptr;

  BenchType _type = BenchType::Float;
  BenchMode _mode = BenchMode::ReadWrite;

  bool _hasRange = false;
  float _min = 0;
  float _max = 0;
  int8_t _precision = -1;

  float _lastNumber = 0;
  bool _lastBool = false;
  bool _primed = false;

  bool readNumber(float &out) const;
  bool readBool() const;
  void writeNumber(float value);
  void writeBool(bool value);
};

class Bench {
public:
  explicit Bench(const char *deviceName, uint16_t port = 81);
  ~Bench();

  BenchChannel &number(const char *key, const char *name, float *value);
  BenchChannel &number(const char *key, const char *name, int *value);
  BenchChannel &boolean(const char *key, const char *name, bool *value);

  // mDNS name, without the ".local" suffix. Call before begin().
  void setHostname(const char *hostname) { _hostname = hostname; }

  void begin();
  void loop();

  void log(const String &message);
  void warn(const String &message);
  void error(const String &message);

  bool hasClients() const { return _clientCount > 0; }
  uint8_t channelCount() const { return _channelCount; }

  void setUpdateInterval(uint16_t ms) { _updateInterval = ms; }
  void setRefreshInterval(uint16_t ms) { _refreshInterval = ms; }

private:
  BenchChannel *addChannel(const char *key, const char *name, BenchType type, void *ptr);

  void onEvent(uint8_t client, int type, uint8_t *payload, size_t length);
  void handleText(uint8_t client, const char *text, size_t length);

  void startMdns();
  void sendHello(uint8_t client);
  void sendAllValues(int16_t client);
  void publishChanges(bool force);
  void sendLine(int16_t client, String &frame);
  void emitLog(const String &message, const char *level);

  static void appendEscaped(String &out, const char *text);
  static String formatNumber(float value, int8_t precision);

  const char *_deviceName;
  const char *_hostname = "bench";
  uint16_t _port;
  WebSocketsServer *_server = nullptr;

  BenchChannel _channels[BENCH_MAX_CHANNELS];
  uint8_t _channelCount = 0;
  uint8_t _clientCount = 0;

  uint16_t _updateInterval = 100;
  uint16_t _refreshInterval = 2000;
  uint32_t _lastUpdate = 0;
  uint32_t _lastRefresh = 0;
  bool _started = false;
};

#endif
