#pragma once
#include <Arduino.h>
#include "ConfigManager.h"

struct ProbeRing {
  static const int N = 10;
  bool filled = false;
  int  head   = 0;
  float co2[N], temp[N], hum[N], db[N];
  unsigned long ts[N];
  void push(float c, float t, float h, float d, unsigned long now) {
    co2[head]=c; temp[head]=t; hum[head]=h; db[head]=d; ts[head]=now;
    head = (head + 1) % N;
    if (head == 0) filled = true;
  }
};

class SensorHandler {
public:
  explicit SensorHandler(ConfigManager* cfg) : _cfg(cfg) {}
  void setSensorSerial(HardwareSerial* s) { _sensorSerial = s; }
  void setDiagSerial(HardwareSerial* s)   { _diagSerial = s; }

  // Called with full text line from Sensor UART
  void handleSensorLine(const String& line);

private:
  ConfigManager*  _cfg;
  HardwareSerial* _sensorSerial = nullptr;
  HardwareSerial* _diagSerial   = nullptr;

  struct ProbeEntry { String id; ProbeRing ring; };
  ProbeEntry _probes[32];
  int        _probeCount = 0;

  ProbeRing* upsertRing(const String& id);
  void updateAreaLive(const String& probeId, float co2, float t, float h, float d);

  void ackSensor(const String& s);
  void handleProbeAssignFromProbe(const String& probeId, const String& area, const String& loc);
  void handleSetProbes(const String& probeId, const String& area, const String& loc);
  void handleRemoveProbe(const String& probeId);

  bool parseTelemetry(const String& body, float& co2, float& t, float& h, float& d);
};
