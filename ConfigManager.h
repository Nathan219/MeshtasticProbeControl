#pragma once
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "Metrics.h"

struct ProbeConfig {
  String id;        // probe shortname (e.g., "0f4c")
  String location;  // e.g., "Hallway"
};

struct ThresholdSet {
  float values[6] = {-1,-1,-1,-1,-1,-1};
};

struct AreaRuntime {
  bool  inited[MET_COUNT] = {false,false,false,false};
  float liveMin[MET_COUNT] = {0};
  float liveMax[MET_COUNT] = {0};
  int   lastPixel = 0;  
};

struct AreaConfig {
  String name;
  std::vector<ProbeConfig> probes;  // <-- multiple probes now
  AreaRuntime rt;
  float overrideMin;
  float overrideMax;
  bool useBaseline;
  ThresholdSet thresholds[MET_COUNT];
};

struct GlobalConfig {
  bool  easterEgg = false;
  char  aggregateMode = 'A';
  int   aggregateN = 3;
  unsigned long ledUpdateInterval = 5000;       // ms between LED updates
  unsigned long diagPixelInterval = 180000;     // ms between diagnostic prints
};


// ================================================================
//  ConfigManager
// ================================================================
class ConfigManager {
public:
  ConfigManager() {}

  GlobalConfig global;  // ✅ new global config block

  bool loadFromFS();
  bool save();
  void ensureDefaults();
  void toJson(JsonDocument& doc);

  // --- Area management ---
  std::vector<AreaConfig>& areas() { return _areas; }
  AreaConfig* findAreaByName(const String& name);
  AreaConfig* findAreaByProbe(const String& probe);

  bool setProbe(const String& probe, const String& area, const String& loc);
  bool removeProbe(const String& probe);

  bool setOverride(const String& area, bool isMin, float val);
  bool setThreshold(const String& area, Metric m, int pix, float val);
  float getThreshold(const String& area, Metric m, int pix) const;
  bool getUseBaseline(const String& area, bool& out);
  bool setUseBaseline(const String& area, bool val);

  bool setStatsInterval(unsigned long val);
  unsigned long getStatsInterval() const;

  bool setConfigValue(const String& key, const String& val);
  String getConfigString() const;

private:
  std::vector<AreaConfig> _areas;
  unsigned long statsIntervalMs = 10000;
};
