#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

// ================================================================
//  Metric Enumeration
// ================================================================
enum Metric {
  MET_CO2 = 0,
  MET_TEMP,
  MET_HUM,
  MET_DB,
  MET_COUNT
};

// Forward declarations
Metric metricFromString(const String& s);
const char* metricToString(Metric m);

// ================================================================
//  Data Structures
// ================================================================
struct ThresholdSet {
  float values[6];
};

struct RealtimeStats {
  bool  inited[MET_COUNT] = {false,false,false,false};
  float liveMin[MET_COUNT] = {0,0,0,0};
  float liveMax[MET_COUNT] = {0,0,0,0};
};

struct AreaConfig {
  String name;
  String location;
  String probeId;
  float minBase = 0.0f;
  float maxBase = 1.0f;
  float overrideMin = -1.0f;
  float overrideMax = -1.0f;
  bool useBaseline = true;
  ThresholdSet thresholds[MET_COUNT];
  RealtimeStats rt;
};

// ================================================================
//  Config Manager
// ================================================================
class ConfigManager {
public:
  bool begin();
  bool save();

  std::vector<AreaConfig>& areas() { return _areas; }

  // Area management
  AreaConfig* findAreaByName(const String& name);
  AreaConfig* findAreaByProbe(const String& probeId);
  bool setProbe(const String& probeId, const String& areaName, const String& location);
  bool removeProbe(const String& probeId);

  // Settings
  bool setOverride(const String& areaName, bool isMin, float value);
  bool setThreshold(const String& areaName, Metric m, int pixelIndex1to6, float value);
  bool setUseBaseline(const String& areaName, bool enabled);
  bool getUseBaseline(const String& areaName, bool& out);

  // Stats interval configuration
  bool setStatsInterval(unsigned long val);
  unsigned long getStatsInterval() const;

private:
  std::vector<AreaConfig> _areas;
  unsigned long statsIntervalMs = 10000; // default 10s

  // JSON helpers
  bool loadFromFS();
  bool writeToFS();
  void toJson(JsonDocument& doc);
  void loadAreaFromJson(AreaConfig& a, JsonObject obj);
  void ensureDefaults();
};
