#include "ConfigManager.h"
#include "Baselines.h"

static const char* CONFIG_PATH = "/config.json";

String toUpperCopy(const String& s) {
  String out = s;
  out.toUpperCase();
  return out;
}

static void fillThresholdsDefault(AreaConfig& a) {
  const float co2[6]  = {400, 600, 800, 1000, 1200, 1400};
  const float tmp[6]  = {18, 19, 20, 21, 22, 23};
  const float hum[6]  = {30, 40, 50, 60, 70, 80};
  const float dbv[6]  = {-1, -1, -1, -1, -1, -1};
  for (int i=0;i<6;i++) {
    a.thresholds[MET_CO2].values[i]  = co2[i];
    a.thresholds[MET_TEMP].values[i] = tmp[i];
    a.thresholds[MET_HUM].values[i]  = hum[i];
    a.thresholds[MET_DB].values[i]   = dbv[i];
  }
}

bool ConfigManager::begin() {
  if (!LittleFS.begin(true)) return false;
  if (!loadFromFS()) {
    ensureDefaults();
    return save();
  }
  for (auto& a : _areas) {
    a.name = toUpperCopy(a.name);
    a.location = toUpperCopy(a.location);
    a.probeId = toUpperCopy(a.probeId);
  }
  return true;
}

bool ConfigManager::save() { return writeToFS(); }

void ConfigManager::ensureDefaults() {
  _areas.clear();
  const char* names[] = {"FLOOR11","FLOOR12","FLOOR15","FLOOR16","FLOOR17","POOL","TEAROOM"};
  for (const char* nm : names) {
    AreaConfig a;
    a.name = nm;
    a.location = "";
    a.probeId  = "";
    a.minBase  = 0.0f;
    a.maxBase  = 1.0f;
    a.overrideMin = -1.0f;
    a.overrideMax = -1.0f;
    a.useBaseline = true;
    fillThresholdsDefault(a);
    _areas.push_back(a);
  }
  statsIntervalMs = 10000;  // default
}

bool ConfigManager::loadFromFS() {
  if (!LittleFS.exists(CONFIG_PATH)) return false;
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return false;
  DynamicJsonDocument doc(8192);
  auto err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  _areas.clear();
  JsonArray arr = doc["areas"].as<JsonArray>();
  if (arr.isNull()) return false;
  for (JsonObject o : arr) {
    AreaConfig a;
    loadAreaFromJson(a, o);
    a.name     = toUpperCopy(a.name);
    a.location = toUpperCopy(a.location);
    a.probeId  = toUpperCopy(a.probeId);
    _areas.push_back(a);
  }
  statsIntervalMs = doc["statsIntervalMs"] | 10000UL;
  return true;
}

void ConfigManager::loadAreaFromJson(AreaConfig& a, JsonObject obj) {
  a.name = obj["name"].as<String>();
  a.location = obj["location"].as<String>();
  a.probeId  = obj["probeId"].as<String>();
  a.minBase = obj["min"] | 0.0f;
  a.maxBase = obj["max"] | 1.0f;
  a.overrideMin = obj["overrideMin"] | -1.0f;
  a.overrideMax = obj["overrideMax"] | -1.0f;
  a.useBaseline = obj["useBaseline"] | true;
  JsonObject th = obj["thresholds"].as<JsonObject>();
  auto loadSet = [&](Metric m, const char* key){
    JsonArray v = th[key].as<JsonArray>();
    for (int i=0;i<6;i++) a.thresholds[m].values[i] = (v.isNull()||v.size()<=i) ? -1.0f : (float)v[i];
  };
  loadSet(MET_CO2,  "CO2");
  loadSet(MET_TEMP, "TEMP");
  loadSet(MET_HUM,  "HUM");
  loadSet(MET_DB,   "DB");
}

bool ConfigManager::writeToFS() {
  DynamicJsonDocument doc(8192);
  toJson(doc);
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  bool ok = (serializeJson(doc, f) > 0);
  f.close();
  return ok;
}

void ConfigManager::toJson(JsonDocument& doc) {
  JsonArray arr = doc.createNestedArray("areas");
  for (auto& a : _areas) {
    JsonObject o = arr.createNestedObject();
    o["name"]        = a.name;
    o["location"]    = a.location;
    o["probeId"]     = a.probeId;
    o["min"]         = a.minBase;
    o["max"]         = a.maxBase;
    o["overrideMin"] = a.overrideMin;
    o["overrideMax"] = a.overrideMax;
    o["useBaseline"] = a.useBaseline;
    JsonObject th = o.createNestedObject("thresholds");
    auto put = [&](Metric m, const char* k){
      JsonArray v = th.createNestedArray(k);
      for (int i=0;i<6;i++) v.add(a.thresholds[m].values[i]);
    };
    put(MET_CO2,  "CO2");
    put(MET_TEMP, "TEMP");
    put(MET_HUM,  "HUM");
    put(MET_DB,   "DB");
  }
  doc["statsIntervalMs"] = statsIntervalMs;
}

AreaConfig* ConfigManager::findAreaByName(const String& name) {
  String up = toUpperCopy(name);
  for (auto& a : _areas) if (a.name == up) return &a;
  return nullptr;
}

AreaConfig* ConfigManager::findAreaByProbe(const String& probeId) {
  String up = toUpperCopy(probeId);
  for (auto& a : _areas) if (a.probeId == up && up.length()) return &a;
  return nullptr;
}

bool ConfigManager::setProbe(const String& probeId, const String& areaName, const String& location) {
  AreaConfig* a = findAreaByName(areaName);
  if (!a) return false;
  a->probeId  = toUpperCopy(probeId);
  if (location.length()) a->location = toUpperCopy(location);
  return save();
}

bool ConfigManager::removeProbe(const String& probeId) {
  String up = toUpperCopy(probeId);
  for (auto& a : _areas) {
    if (a.probeId == up) {
      a.probeId = "";
      return save();
    }
  }
  return false;
}

bool ConfigManager::setOverride(const String& areaName, bool isMin, float value) {
  AreaConfig* a = findAreaByName(areaName);
  if (!a) return false;
  if (isMin) a->overrideMin = value;
  else       a->overrideMax = value;
  return save();
}

bool ConfigManager::setThreshold(const String& areaName, Metric m, int pixelIndex1to6, float value) {
  if (m >= MET_COUNT || pixelIndex1to6 < 1 || pixelIndex1to6 > 6) return false;
  AreaConfig* a = findAreaByName(areaName);
  if (!a) return false;
  a->thresholds[m].values[pixelIndex1to6 - 1] = value;
  return save();
}

bool ConfigManager::setUseBaseline(const String& areaName, bool enabled) {
  AreaConfig* a = findAreaByName(areaName);
  if (!a) return false;
  a->useBaseline = enabled;
  return save();
}

bool ConfigManager::getUseBaseline(const String& areaName, bool& out) {
  AreaConfig* a = findAreaByName(areaName);
  if (!a) return false;
  out = a->useBaseline;
  return true;
}

// ================================================================
//  Stats interval handling
// ================================================================
bool ConfigManager::setStatsInterval(unsigned long val) {
  if (val < 1000 || val > 60000) return false;
  statsIntervalMs = val;
  return save();
}

unsigned long ConfigManager::getStatsInterval() const {
  return statsIntervalMs;
}

// ================================================================
//  Metric helpers
// ================================================================
Metric metricFromString(const String& s) {
  if (s.equalsIgnoreCase("CO2"))  return MET_CO2;
  if (s.equalsIgnoreCase("TEMP")) return MET_TEMP;
  if (s.equalsIgnoreCase("HUM"))  return MET_HUM;
  if (s.equalsIgnoreCase("DB") || s.equalsIgnoreCase("SOUND")) return MET_DB;
  return MET_COUNT;
}

const char* metricToString(Metric m) {
  switch (m) {
    case MET_CO2:  return "CO2";
    case MET_TEMP: return "TEMP";
    case MET_HUM:  return "HUM";
    case MET_DB:   return "DB";
    default:       return "?";
  }
}
