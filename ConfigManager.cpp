#include "ConfigManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

#define CONFIG_PATH "/config.json"

// ================================================================
bool ConfigManager::loadFromFS() {
  if (!LittleFS.exists(CONFIG_PATH)) {
    ensureDefaults();
    save();
    return true;
  }

  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return false;

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, f)) {
    f.close();
    ensureDefaults();
    return false;
  }
  f.close();

  statsIntervalMs = doc["statsIntervalMs"] | 10000UL;
  _areas.clear();

  for (JsonObject o : doc["areas"].as<JsonArray>()) {
    AreaConfig a;
    a.name        = (const char*)o["name"];
    a.location    = (const char*)o["location"];
    a.probeId     = (const char*)o["probeId"];
    a.useBaseline = o["useBaseline"] | true;
    a.overrideMin = o["overrideMin"] | -1.0f;
    a.overrideMax = o["overrideMax"] | -1.0f;

    JsonArray thArr = o["thresholds"].as<JsonArray>();
    for (size_t i=0; i<thArr.size() && i<MET_COUNT; i++) {
      JsonArray row = thArr[i];
      for (size_t j=0; j<row.size() && j<6; j++)
        a.thresholds[i].values[j] = row[j].as<float>();
    }
    _areas.push_back(a);
  }
  return true;
}

// ================================================================
bool ConfigManager::save() {
  DynamicJsonDocument doc(8192);
  toJson(doc);
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

// ================================================================
void ConfigManager::toJson(JsonDocument& doc) {
  doc["statsIntervalMs"] = statsIntervalMs;
  JsonArray arr = doc.createNestedArray("areas");
  for (auto& a : _areas) {
    JsonObject o = arr.createNestedObject();
    o["name"]        = a.name;
    o["location"]    = a.location;
    o["probeId"]     = a.probeId;
    o["useBaseline"] = a.useBaseline;
    o["overrideMin"] = a.overrideMin;
    o["overrideMax"] = a.overrideMax;
    JsonArray thArr = o.createNestedArray("thresholds");
    for (int i=0; i<MET_COUNT; i++) {
      JsonArray row = thArr.createNestedArray();
      for (int j=0; j<6; j++) row.add(a.thresholds[i].values[j]);
    }
  }
}

// ================================================================
void ConfigManager::ensureDefaults() {
  _areas.clear();
  statsIntervalMs = 10000;
  const char* defaults[] = {"FLOOR11","FLOOR12","FLOOR15","FLOOR16","FLOOR17","POOL","TEAROOM"};
  for (auto& n : defaults) {
    AreaConfig a;
    a.name = n;
    _areas.push_back(a);
  }
}

// ================================================================
AreaConfig* ConfigManager::findAreaByName(const String& name) {
  for (auto& a : _areas)
    if (a.name.equalsIgnoreCase(name))
      return &a;
  return nullptr;
}

AreaConfig* ConfigManager::findAreaByProbe(const String& probe) {
  for (auto& a : _areas)
    if (a.probeId.equalsIgnoreCase(probe))
      return &a;
  return nullptr;
}

// ================================================================
bool ConfigManager::setProbe(const String& probe, const String& area, const String& loc) {
  AreaConfig* a = findAreaByName(area);
  if (!a) return false;
  a->probeId = probe;
  a->location = loc;
  return save();
}

bool ConfigManager::removeProbe(const String& probe) {
  for (auto& a : _areas)
    if (a.probeId.equalsIgnoreCase(probe)) {
      a.probeId = "";
      return save();
    }
  return false;
}

// ================================================================
bool ConfigManager::setOverride(const String& area, bool isMin, float val) {
  AreaConfig* a = findAreaByName(area);
  if (!a) return false;
  if (isMin) a->overrideMin = val; else a->overrideMax = val;
  return save();
}

bool ConfigManager::setThreshold(const String& area, Metric m, int pix, float val) {
  
  for (auto& a : _areas) {
    if (a.name.equalsIgnoreCase(area)) {
      if (pix < 1 || pix > 6) return false;
      a.thresholds[m].values[pix - 1] = val;
      return save();
    }
  }
  return false;
}

float ConfigManager::getThreshold(const String& area, Metric m, int pix) const {
  for (const auto& a : _areas) {
    if (a.name.equalsIgnoreCase(area)) {
      if (pix < 1 || pix > 6) return -1;
      return a.thresholds[m].values[pix - 1];
    }
  }
  return -1;
}

bool ConfigManager::getUseBaseline(const String& area, bool& out) {
  AreaConfig* a = findAreaByName(area);
  if (!a) return false;
  out = a->useBaseline;
  return true;
}

bool ConfigManager::setUseBaseline(const String& area, bool val) {
  AreaConfig* a = findAreaByName(area);
  if (!a) return false;
  a->useBaseline = val;
  return save();
}

// ================================================================
bool ConfigManager::setStatsInterval(unsigned long val) {
  if (val < 1000 || val > 60000) return false;
  statsIntervalMs = val;
  return save();
}

unsigned long ConfigManager::getStatsInterval() const {
  return statsIntervalMs;
}
