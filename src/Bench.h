/*
  Bench — build a phone control panel for your board with no cloud in the middle.

  Register the variables you care about, call begin() and loop(), and the Bench
  app discovers them automatically over a plain WebSocket on your own network.

  Requires: WebSockets (Links2004) and ArduinoJson (v7).
  Boards:   ESP32, ESP8266.
*/

#ifndef BENCH_H
#define BENCH_H

#include <Arduino.h>

#ifndef BENCH_MAX_CHANNELS
#define BENCH_MAX_CHANNELS 24
#endif

class WebSocketsServer;

enum class BenchType : uint8_t { Bool, Int, Float };

enum class BenchMode : uint8_t { Read, Write, ReadWrite };

/**
 * One variable exposed to the app. Returned by Bench::number() and
 * Bench::boolean() so options can be chained onto the registration.
 */
class BenchChannel {
 public:
  BenchChannel& unit(const char* text);
  BenchChannel& range(float low, float high);
  /** Decimal places the app should display. */
  BenchChannel& precision(uint8_t places);
  /** The app may show this value but never write it. */
  BenchChannel& readOnly();
  /** The app may write this value but never displays it (momentary triggers). */
  BenchChannel& writeOnly();
  /** Human label shown on the panel. Defaults to the one passed at registration. */
  BenchChannel& label(const char* text);

 private:
  friend class Bench;

  const char* _key = nullptr;
  const char* _name = nullptr;
  const char* _unit = nullptr;
  BenchType _type = BenchType::Float;
  BenchMode _mode = BenchMode::ReadWrite;

  bool _hasRange = false;
  float _min = 0;
  float _max = 0;
  int8_t _precision = -1;

  void* _ptr = nullptr;

  // Change detection so an idle board stays quiet on the wire.
  float _lastNumber = 0;
  bool _lastBool = false;
  bool _primed = false;

  bool readNumber(float& out) const;
  bool readBool() const;
  void writeNumber(float value);
  void writeBool(bool value);
};

class Bench {
 public:
  explicit Bench(const char* deviceName, uint16_t port = 81);
  ~Bench();

  BenchChannel& number(const char* key, const char* name, float* value);
  BenchChannel& number(const char* key, const char* name, int* value);
  BenchChannel& boolean(const char* key, const char* name, bool* value);

  /**
   * mDNS hostname, without the ".local" suffix. The app probes "bench.local"
   * during discovery, so leaving this at the default makes the board findable
   * without typing an IP address. Call before begin().
   */
  void setHostname(const char* hostname) { _hostname = hostname; }

  /** Start the server and advertise over mDNS. Call after WiFi is connected. */
  void begin();
  /** Pump the server and publish changed values. Call every loop(). */
  void loop();

  /** Push a line to the app's terminal widget. */
  void log(const String& message);
  void warn(const String& message);
  void error(const String& message);

  /** How often changed values are published, in milliseconds. Default 100. */
  void setUpdateInterval(uint16_t ms) { _updateInterval = ms; }
  /** Full state refresh interval, so charts stay continuous. Default 2000. */
  void setRefreshInterval(uint16_t ms) { _refreshInterval = ms; }

  bool hasClients() const { return _clientCount > 0; }

 private:
  BenchChannel* addChannel(const char* key, const char* name, BenchType type, void* ptr);

  void onEvent(uint8_t client, int type, uint8_t* payload, size_t length);
  void handleText(uint8_t client, const char* text, size_t length);

  void startMdns();
  void sendHello(uint8_t client);
  void sendAllValues(int16_t client);
  void publishChanges(bool force);
  void sendLine(int16_t client, const String& frame);
  void emitLog(const String& message, const char* level);

  static void appendEscaped(String& out, const char* text);
  static String formatNumber(float value, int8_t precision);

  const char* _deviceName;
  const char* _hostname = "bench";
  uint16_t _port;
  WebSocketsServer* _server = nullptr;

  BenchChannel _channels[BENCH_MAX_CHANNELS];
  uint8_t _channelCount = 0;
  uint8_t _clientCount = 0;

  uint16_t _updateInterval = 100;
  uint16_t _refreshInterval = 2000;
  uint32_t _lastUpdate = 0;
  uint32_t _lastRefresh = 0;
  bool _started = false;
};

#endif  // BENCH_H
