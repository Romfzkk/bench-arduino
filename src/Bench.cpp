// Bench - phone control panel for ESP32 / ESP8266 projects.
// Copyright (c) 2026 romfzk. MIT licence, see LICENSE.

#include "Bench.h"

#include <ArduinoJson.h>
#include <WebSocketsServer.h>

#if defined(ESP32)
#include <ESPmDNS.h>
#elif defined(ESP8266)
#include <ESP8266mDNS.h>
#endif

namespace {

// Sensor noise below this is not a change worth a packet.
const float kEpsilon = 0.0001f;

const char *modeToken(BenchMode mode) {
  switch (mode) {
  case BenchMode::Read:
    return "r";
  case BenchMode::Write:
    return "w";
  default:
    return "rw";
  }
}

const char *typeToken(BenchType type) {
  return type == BenchType::Bool ? "bool" : "number";
}

} // namespace

BenchChannel &BenchChannel::unit(const char *text) {
  _unit = text;
  return *this;
}

BenchChannel &BenchChannel::range(float low, float high) {
  _hasRange = true;
  _min = low;
  _max = high;
  return *this;
}

BenchChannel &BenchChannel::precision(uint8_t places) {
  _precision = (int8_t)places;
  return *this;
}

BenchChannel &BenchChannel::label(const char *text) {
  _name = text;
  return *this;
}

BenchChannel &BenchChannel::readOnly() {
  _mode = BenchMode::Read;
  return *this;
}

BenchChannel &BenchChannel::writeOnly() {
  _mode = BenchMode::Write;
  return *this;
}

bool BenchChannel::readNumber(float &out) const {
  if (!_ptr)
    return false;

  if (_type == BenchType::Float) {
    out = *(float *)_ptr;
    return true;
  }
  if (_type == BenchType::Int) {
    out = (float)*(int *)_ptr;
    return true;
  }
  return false;
}

bool BenchChannel::readBool() const {
  return _ptr ? *(bool *)_ptr : false;
}

void BenchChannel::writeNumber(float value) {
  if (!_ptr)
    return;

  if (_hasRange) {
    if (value < _min)
      value = _min;
    if (value > _max)
      value = _max;
  }

  if (_type == BenchType::Float)
    *(float *)_ptr = value;
  else if (_type == BenchType::Int)
    *(int *)_ptr = (int)lroundf(value);
}

void BenchChannel::writeBool(bool value) {
  if (_ptr)
    *(bool *)_ptr = value;
}

Bench::Bench(const char *deviceName, uint16_t port)
    : _deviceName(deviceName), _port(port) {}

Bench::~Bench() {
  delete _server;
}

BenchChannel *Bench::addChannel(const char *key, const char *name, BenchType type, void *ptr) {
  if (_channelCount >= BENCH_MAX_CHANNELS) {
    // Silently dropping it would leave a widget bound to nothing, which is
    // far harder to debug than a line on the serial monitor.
    Serial.println(F("[Bench] out of channels, raise BENCH_MAX_CHANNELS"));
    return &_channels[BENCH_MAX_CHANNELS - 1];
  }

  BenchChannel &channel = _channels[_channelCount++];
  channel._key = key;
  channel._name = name;
  channel._type = type;
  channel._ptr = ptr;
  channel._mode = BenchMode::ReadWrite;
  return &channel;
}

BenchChannel &Bench::number(const char *key, const char *name, float *value) {
  return *addChannel(key, name, BenchType::Float, value);
}

BenchChannel &Bench::number(const char *key, const char *name, int *value) {
  return *addChannel(key, name, BenchType::Int, value);
}

BenchChannel &Bench::boolean(const char *key, const char *name, bool *value) {
  return *addChannel(key, name, BenchType::Bool, value);
}

void Bench::begin() {
  if (_started)
    return;

  _server = new WebSocketsServer(_port);
  _server->begin();
  _server->onEvent([this](uint8_t client, WStype_t type, uint8_t *payload, size_t length) {
    this->onEvent(client, (int)type, payload, length);
  });
  _started = true;

  startMdns();

  Serial.print(F("[Bench] listening on "));
  Serial.println(_port);
}

void Bench::loop() {
  if (!_started)
    return;

  _server->loop();

#if defined(ESP8266)
  MDNS.update();
#endif

  uint32_t now = millis();
  if (now - _lastUpdate < _updateInterval)
    return;
  _lastUpdate = now;

  bool force = (now - _lastRefresh) >= _refreshInterval;
  if (force)
    _lastRefresh = now;

  publishChanges(force);
}

void Bench::startMdns() {
#if defined(ESP32) || defined(ESP8266)
  if (!MDNS.begin(_hostname)) {
    Serial.println(F("[Bench] mDNS failed, connect by IP instead"));
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

void Bench::onEvent(uint8_t client, int type, uint8_t *payload, size_t length) {
  switch ((WStype_t)type) {
  case WStype_CONNECTED:
    if (_clientCount < 255)
      _clientCount++;
    sendHello(client);
    sendAllValues(client);
    break;

  case WStype_DISCONNECTED:
    if (_clientCount > 0)
      _clientCount--;
    break;

  case WStype_TEXT:
    handleText(client, (const char *)payload, length);
    break;

  default:
    break;
  }
}

void Bench::handleText(uint8_t client, const char *text, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, text, length))
    return;

  const char *t = doc["t"];
  if (!t)
    return;

  if (!strcmp(t, "get")) {
    sendHello(client);
    sendAllValues(client);
    return;
  }

  if (!strcmp(t, "ping")) {
    String frame = F("{\"t\":\"pong\",\"ts\":");
    frame += doc["ts"].as<uint32_t>();
    frame += '}';
    _server->sendTXT(client, frame);
    return;
  }

  if (strcmp(t, "set"))
    return;

  const char *key = doc["k"];
  if (!key)
    return;

  for (uint8_t i = 0; i < _channelCount; i++) {
    BenchChannel &channel = _channels[i];
    if (!channel._key || strcmp(channel._key, key))
      continue;

    if (channel._mode == BenchMode::Read)
      return;

    if (channel._type == BenchType::Bool)
      channel.writeBool(doc["v"].as<bool>());
    else
      channel.writeNumber(doc["v"].as<float>());

    // Echo immediately so a second phone sees the change too.
    publishChanges(true);
    return;
  }
}

void Bench::sendHello(uint8_t client) {
  String frame = F("{\"t\":\"hello\",\"proto\":");
  frame += BENCH_PROTOCOL;
  frame += F(",\"fw\":\"");
  frame += F(BENCH_VERSION);
  frame += F("\",\"id\":\"");
  appendEscaped(frame, _deviceName);
  frame += F("\",\"name\":\"");
  appendEscaped(frame, _deviceName);
  frame += F("\",\"channels\":[");

  for (uint8_t i = 0; i < _channelCount; i++) {
    const BenchChannel &channel = _channels[i];

    if (i)
      frame += ',';

    frame += F("{\"k\":\"");
    appendEscaped(frame, channel._key);
    frame += F("\",\"n\":\"");
    appendEscaped(frame, channel._name ? channel._name : channel._key);
    frame += F("\",\"type\":\"");
    frame += typeToken(channel._type);
    frame += F("\",\"mode\":\"");
    frame += modeToken(channel._mode);
    frame += '"';

    if (channel._unit) {
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
  String frame = F("{\"t\":\"vs\",\"d\":{");
  bool first = true;

  for (uint8_t i = 0; i < _channelCount; i++) {
    BenchChannel &channel = _channels[i];
    if (channel._mode == BenchMode::Write)
      continue;

    if (!first)
      frame += ',';
    first = false;

    frame += '"';
    appendEscaped(frame, channel._key);
    frame += F("\":");

    if (channel._type == BenchType::Bool) {
      bool value = channel.readBool();
      channel._lastBool = value;
      frame += value ? F("true") : F("false");
    } else {
      float value = 0;
      channel.readNumber(value);
      channel._lastNumber = value;
      frame += formatNumber(value, channel._precision);
    }

    // Priming here means the next publishChanges() only sends real changes.
    channel._primed = true;
  }

  frame += F("}}");
  sendLine(client, frame);
}

void Bench::publishChanges(bool force) {
  if (!_clientCount)
    return;

  String body;
  bool any = false;

  for (uint8_t i = 0; i < _channelCount; i++) {
    BenchChannel &channel = _channels[i];
    if (channel._mode == BenchMode::Write)
      continue;

    bool changed = force || !channel._primed;

    if (channel._type == BenchType::Bool) {
      bool value = channel.readBool();
      if (value != channel._lastBool)
        changed = true;
      if (!changed)
        continue;

      channel._lastBool = value;
      channel._primed = true;

      if (any)
        body += ',';
      any = true;
      body += '"';
      appendEscaped(body, channel._key);
      body += F("\":");
      body += value ? F("true") : F("false");
    } else {
      float value = 0;
      if (!channel.readNumber(value))
        continue;
      if (fabsf(value - channel._lastNumber) > kEpsilon)
        changed = true;
      if (!changed)
        continue;

      channel._lastNumber = value;
      channel._primed = true;

      if (any)
        body += ',';
      any = true;
      body += '"';
      appendEscaped(body, channel._key);
      body += F("\":");
      body += formatNumber(value, channel._precision);
    }
  }

  if (!any)
    return;

  String frame = F("{\"t\":\"vs\",\"d\":{");
  frame += body;
  frame += F("}}");
  sendLine(-1, frame);
}

void Bench::log(const String &message) {
  emitLog(message, "info");
}

void Bench::warn(const String &message) {
  emitLog(message, "warn");
}

void Bench::error(const String &message) {
  emitLog(message, "error");
}

void Bench::emitLog(const String &message, const char *level) {
  if (!_started || !_clientCount)
    return;

  String frame = F("{\"t\":\"log\",\"lvl\":\"");
  frame += level;
  frame += F("\",\"m\":\"");
  appendEscaped(frame, message.c_str());
  frame += F("\"}");
  sendLine(-1, frame);
}

void Bench::sendLine(int16_t client, String &frame) {
  if (!_server)
    return;

  if (client < 0)
    _server->broadcastTXT(frame);
  else
    _server->sendTXT((uint8_t)client, frame);
}

void Bench::appendEscaped(String &out, const char *text) {
  if (!text)
    return;

  for (const char *p = text; *p; p++) {
    char c = *p;
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
      // Raw control characters would make the frame invalid JSON.
      if ((uint8_t)c >= 0x20)
        out += c;
      break;
    }
  }
}

String Bench::formatNumber(float value, int8_t precision) {
  if (isnan(value) || isinf(value))
    return String("0");

  uint8_t places = precision >= 0 ? (uint8_t)precision : 3;
  String text = String(value, (unsigned int)places);

  if (places) {
    int end = text.length();
    while (end > 0 && text[end - 1] == '0')
      end--;
    if (end > 0 && text[end - 1] == '.')
      end--;
    text = text.substring(0, end);
  }

  return text.length() ? text : String("0");
}
