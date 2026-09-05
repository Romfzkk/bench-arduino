#include "Bench.h"

#include <ArduinoJson.h>
#include <WebSocketsServer.h>

#if defined(ESP32)
#include <ESPmDNS.h>
#elif defined(ESP8266)
#include <ESP8266mDNS.h>
#endif

namespace {
/** Values closer than this are treated as unchanged, so sensor noise stays off the wire. */
constexpr float kEpsilon = 0.0001f;

const char* modeToken(BenchMode mode) {
  switch (mode) {
    case BenchMode::Read:
      return "r";
    case BenchMode::Write:
      return "w";
    default:
      return "rw";
  }
}

const char* typeToken(BenchType type) {
  return type == BenchType::Bool ? "bool" : "number";
}
}  // namespace

/* ------------------------------------------------------------- BenchChannel */

BenchChannel& BenchChannel::unit(const char* text) {
  _unit = text;
  return *this;
}

BenchChannel& BenchChannel::range(float low, float high) {
  _hasRange = true;
  _min = low;
  _max = high;
  return *this;
}

BenchChannel& BenchChannel::precision(uint8_t places) {
  _precision = static_cast<int8_t>(places);
  return *this;
}

BenchChannel& BenchChannel::readOnly() {
  _mode = BenchMode::Read;
  return *this;
}

BenchChannel& BenchChannel::writeOnly() {
  _mode = BenchMode::Write;
  return *this;
}

BenchChannel& BenchChannel::label(const char* text) {
  _name = text;
  return *this;
}

bool BenchChannel::readNumber(float& out) const {
  if (_ptr == nullptr) return false;
  switch (_type) {
    case BenchType::Float:
      out = *static_cast<float*>(_ptr);
      return true;
    case BenchType::Int:
      out = static_cast<float>(*static_cast<int*>(_ptr));
      return true;
    default:
      return false;
  }
}

bool BenchChannel::readBool() const {
  if (_ptr == nullptr) return false;
  return *static_cast<bool*>(_ptr);
}

void BenchChannel::writeNumber(float value) {
  if (_ptr == nullptr) return;
  if (_hasRange) {
    if (value < _min) value = _min;
    if (value > _max) value = _max;
  }
  if (_type == BenchType::Float) {
    *static_cast<float*>(_ptr) = value;
  } else if (_type == BenchType::Int) {
    *static_cast<int*>(_ptr) = static_cast<int>(lroundf(value));
  }
}

void BenchChannel::writeBool(bool value) {
  if (_ptr == nullptr) return;
  *static_cast<bool*>(_ptr) = value;
}

/* --------------------------------------------------------------------- Bench */

Bench::Bench(const char* deviceName, uint16_t port)
    : _deviceName(deviceName), _port(port) {}

Bench::~Bench() {
  delete _server;
}

BenchChannel* Bench::addChannel(const char* key, const char* name, BenchType type, void* ptr) {
  if (_channelCount >= BENCH_MAX_CHANNELS) {
    // Registering past the limit would silently corrupt the panel, so say so.
    Serial.println(F("[Bench] channel limit reached; raise BENCH_MAX_CHANNELS"));
    return &_channels[BENCH_MAX_CHANNELS - 1];
  }
  BenchChannel& channel = _channels[_channelCount++];
  channel._key = key;
  channel._name = name;
  channel._type = type;
  channel._ptr = ptr;
  channel._mode = BenchMode::ReadWrite;
  return &channel;
}

BenchChannel& Bench::number(const char* key, const char* name, float* value) {
  return *addChannel(key, name, BenchType::Float, value);
}

BenchChannel& Bench::number(const char* key, const char* name, int* value) {
  return *addChannel(key, name, BenchType::Int, value);
}

BenchChannel& Bench::boolean(const char* key, const char* name, bool* value) {
  return *addChannel(key, name, BenchType::Bool, value);
}

void Bench::begin() {
  if (_started) return;
  _server = new WebSocketsServer(_port);
  _server->begin();
  _server->onEvent([this](uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
    this->onEvent(client, static_cast<int>(type), payload, length);
  });
  _started = true;

  startMdns();

  Serial.print(F("[Bench] listening on port "));
  Serial.println(_port);
}

/**
 * Advertise as _bench._tcp so the app can find this board without anyone
 * typing an IP address. The TXT records let the app show a useful name in the
 * results list before it has connected.
 */
void Bench::startMdns() {
#if defined(ESP32) || defined(ESP8266)
  if (!MDNS.begin(_hostname)) {
    Serial.println(F("[Bench] mDNS failed to start; find the board by IP instead"));
    return;
  }
  MDNS.addService("bench", "tcp", _port);
  MDNS.addServiceTxt("bench", "tcp", "name", _deviceName);
  MDNS.addServiceTxt("bench", "tcp", "path", "/ws");
  MDNS.addServiceTxt("bench", "tcp", "channels", String(_channelCount));

  Serial.print(F("[Bench] discoverable as "));
  Serial.print(_hostname);
  Serial.println(F(".local"));
#endif
}

void Bench::loop() {
  if (!_started) return;
  _server->loop();

#if defined(ESP8266)
  // The ESP8266 mDNS responder is pumped from the sketch; the ESP32 one runs
  // on its own task.
  MDNS.update();
#endif

  const uint32_t now = millis();
  if (now - _lastUpdate < _updateInterval) return;
  _lastUpdate = now;

  const bool force = (now - _lastRefresh) >= _refreshInterval;
  if (force) _lastRefresh = now;
  publishChanges(force);
}

void Bench::onEvent(uint8_t client, int type, uint8_t* payload, size_t length) {
  switch (static_cast<WStype_t>(type)) {
    case WStype_CONNECTED:
      if (_clientCount < 255) _clientCount++;
      sendHello(client);
      sendAllValues(client);
      break;

    case WStype_DISCONNECTED:
      if (_clientCount > 0) _clientCount--;
      break;

    case WStype_TEXT:
      handleText(client, reinterpret_cast<const char*>(payload), length);
      break;

    default:
      break;
  }
}

void Bench::handleText(uint8_t client, const char* text, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, text, length)) return;  // Ignore anything malformed.

  const char* t = doc["t"];
  if (t == nullptr) return;

  if (strcmp(t, "get") == 0) {
    sendHello(client);
    sendAllValues(client);
    return;
  }

  if (strcmp(t, "ping") == 0) {
    String frame = F("{\"t\":\"pong\",\"ts\":");
    frame += doc["ts"].as<uint32_t>();
    frame += '}';
    _server->sendTXT(client, frame);
    return;
  }

  if (strcmp(t, "set") != 0) return;

  const char* key = doc["k"];
  if (key == nullptr) return;

  for (uint8_t i = 0; i < _channelCount; i++) {
    BenchChannel& channel = _channels[i];
    if (channel._key == nullptr || strcmp(channel._key, key) != 0) continue;
    if (channel._mode == BenchMode::Read) return;  // The app should not have sent this.

    if (channel._type == BenchType::Bool) {
      channel.writeBool(doc["v"].as<bool>());
    } else {
      channel.writeNumber(doc["v"].as<float>());
    }
    // Confirm straight back so every client stays in step.
    publishChanges(true);
    return;
  }
}

void Bench::sendHello(uint8_t client) {
  String frame = F("{\"t\":\"hello\",\"proto\":1,\"id\":\"");
  appendEscaped(frame, _deviceName);
  frame += F("\",\"name\":\"");
  appendEscaped(frame, _deviceName);
  frame += F("\",\"channels\":[");

  for (uint8_t i = 0; i < _channelCount; i++) {
    const BenchChannel& channel = _channels[i];
    if (i > 0) frame += ',';
    frame += F("{\"k\":\"");
    appendEscaped(frame, channel._key);
    frame += F("\",\"n\":\"");
    appendEscaped(frame, channel._name != nullptr ? channel._name : channel._key);
    frame += F("\",\"type\":\"");
    frame += typeToken(channel._type);
    frame += F("\",\"mode\":\"");
    frame += modeToken(channel._mode);
    frame += '"';
    if (channel._unit != nullptr) {
      frame += F(",\"unit\":\"");
      appendEscaped(frame, channel._unit);
      frame += '"';
    }
    if (channel._hasRange) {
      frame += F(",\"min\":");
      frame += formatNumber(channel._min, 3);
      frame += F(",\"max\":");
      frame += formatNumber(channel._max, 3);
    }
    if (channel._precision >= 0) {
      frame += F(",\"prec\":");
      frame += channel._precision;
    }
    frame += '}';
  }

  frame += F("]}");
  _server->sendTXT(client, frame);
}

void Bench::sendAllValues(int16_t client) {
  // Priming here means the next publishChanges() only sends genuine changes.
  String frame = F("{\"t\":\"vs\",\"d\":{");
  bool first = true;

  for (uint8_t i = 0; i < _channelCount; i++) {
    BenchChannel& channel = _channels[i];
    if (channel._mode == BenchMode::Write) continue;

    if (!first) frame += ',';
    first = false;
    frame += '"';
    appendEscaped(frame, channel._key);
    frame += F("\":");

    if (channel._type == BenchType::Bool) {
      const bool value = channel.readBool();
      channel._lastBool = value;
      frame += value ? F("true") : F("false");
    } else {
      float value = 0;
      channel.readNumber(value);
      channel._lastNumber = value;
      frame += formatNumber(value, channel._precision);
    }
    channel._primed = true;
  }

  frame += F("}}");
  sendLine(client, frame);
}

void Bench::publishChanges(bool force) {
  if (_clientCount == 0) return;

  String body;
  bool any = false;

  for (uint8_t i = 0; i < _channelCount; i++) {
    BenchChannel& channel = _channels[i];
    if (channel._mode == BenchMode::Write) continue;

    bool changed = force || !channel._primed;

    if (channel._type == BenchType::Bool) {
      const bool value = channel.readBool();
      if (value != channel._lastBool) changed = true;
      if (!changed) continue;
      channel._lastBool = value;
      channel._primed = true;

      if (any) body += ',';
      any = true;
      body += '"';
      appendEscaped(body, channel._key);
      body += F("\":");
      body += value ? F("true") : F("false");
    } else {
      float value = 0;
      if (!channel.readNumber(value)) continue;
      if (fabsf(value - channel._lastNumber) > kEpsilon) changed = true;
      if (!changed) continue;
      channel._lastNumber = value;
      channel._primed = true;

      if (any) body += ',';
      any = true;
      body += '"';
      appendEscaped(body, channel._key);
      body += F("\":");
      body += formatNumber(value, channel._precision);
    }
  }

  if (!any) return;

  String frame = F("{\"t\":\"vs\",\"d\":{");
  frame += body;
  frame += F("}}");
  sendLine(-1, frame);
}

void Bench::log(const String& message) { emitLog(message, "info"); }
void Bench::warn(const String& message) { emitLog(message, "warn"); }
void Bench::error(const String& message) { emitLog(message, "error"); }

void Bench::emitLog(const String& message, const char* level) {
  if (!_started || _clientCount == 0) return;
  String frame = F("{\"t\":\"log\",\"lvl\":\"");
  frame += level;
  frame += F("\",\"m\":\"");
  appendEscaped(frame, message.c_str());
  frame += F("\"}");
  sendLine(-1, frame);
}

void Bench::sendLine(int16_t client, const String& frame) {
  if (_server == nullptr) return;
  if (client < 0) {
    _server->broadcastTXT(frame);
  } else {
    _server->sendTXT(static_cast<uint8_t>(client), frame);
  }
}

void Bench::appendEscaped(String& out, const char* text) {
  if (text == nullptr) return;
  for (const char* p = text; *p != '\0'; ++p) {
    const char c = *p;
    switch (c) {
      case '"':
        out += F("\\\"");
        break;
      case '\\':
        out += F("\\\\");
        break;
      case '\n':
        out += F("\\n");
        break;
      case '\r':
        out += F("\\r");
        break;
      case '\t':
        out += F("\\t");
        break;
      default:
        if (static_cast<uint8_t>(c) < 0x20) {
          // Control characters would produce invalid JSON; drop them.
          break;
        }
        out += c;
    }
  }
}

String Bench::formatNumber(float value, int8_t precision) {
  if (isnan(value) || isinf(value)) return String("0");
  // The app rejects non-finite values, so never emit one.
  const uint8_t places = precision >= 0 ? static_cast<uint8_t>(precision) : 3;
  String text = String(value, places);
  if (places > 0) {
    // Trim trailing zeros so the wire stays small.
    int end = text.length();
    while (end > 0 && text[end - 1] == '0') end--;
    if (end > 0 && text[end - 1] == '.') end--;
    text = text.substring(0, end);
  }
  if (text.length() == 0) text = "0";
  return text;
}
