#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include "ConfigManager.h"
#include "Metrics.h"

// ================================================================
//  SensorHandler
// ================================================================
class SensorHandler {
public:
  // Use Stream* so this works with Serial (USB) or HardwareSerial
  SensorHandler(ConfigManager* cfg, Stream* out)
    : _cfg(cfg), _out(out) {}

  // Process a line from sensors UART (CO2, TEMP, HUM, etc. or SET PROBE)
  void handleSensorMessage(const String& line);

  // Optional: assign UARTs for outgoing messages
  void setSensorSerial(HardwareSerial* s) { _sensorSerial = s; }
  void setDiagSerial(HardwareSerial* d)   { _diagSerial = d; }

  // Retrieve last 10 values for a metric or all metrics
  bool getHistory(const String& probe, Metric m, std::vector<float>& out);
  bool getAllHistory(const String& probe, std::vector<std::pair<Metric,std::vector<float>>>& out);


  // Average of lastN samples per probe -> average across probes in the area.
  // Returns NAN if no data.
  float areaMetricRecentAvg(const String& area, Metric m, int lastN);

private:
  ConfigManager* _cfg;
  Stream* _out;  // ✅ Works with both Serial (USB) and HardwareSerial
  HardwareSerial* _sensorSerial = nullptr;
  HardwareSerial* _diagSerial   = nullptr;

  // Per-probe metric history (rolling window)
  std::map<String, std::map<Metric, std::vector<float>>> _history;

  void updateHistory(const String& probe, Metric m, float value);
  void updateAreaStats(const String& probe, Metric m, float value);
};
