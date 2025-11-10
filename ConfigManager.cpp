#include "ConfigManager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

#define FSYSTEM SPIFFS

#define CONFIG_PATH "/config.json"

// ================================================================
bool ConfigManager::loadFromFS() {
  if (!FSYSTEM.exists(CONFIG_PATH)) {
    ensureDefaults();
    save();
    return true;
  }

  File f = FSYSTEM.open(CONFIG_PATH, "r");
  if (!f) return false;

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, f)) {
    f.close();
    ensureDefaults();
    return false;
  }
  f.close();

  statsIntervalMs = doc["statsIntervalMs"] | 10000UL;

  global.diagPixelInterval = doc["global"]["diagPixelInterval"] | 180000UL;

  global.easterEgg   = doc["global"]["easterEgg"]   | false;
  const char* agv    = doc["global"]["aggregateMode"] | "A";
  global.aggregateMode = agv[0];
  global.aggregateN  = doc["global"]["aggregateN"]  | 3;

  _areas.clear();

  for (JsonObject o : doc["areas"].as<JsonArray>()) {
    AreaConfig a;
    a.name        = (const char*)o["name"];
    a.useBaseline = o["useBaseline"] | true;
    a.overrideMin = o["overrideMin"] | -1.0f;
    a.overrideMax = o["overrideMax"] | -1.0f;

    // --- handle probes array ---
    if (o.containsKey("probes")) {
      for (JsonObject p : o["probes"].as<JsonArray>()) {
        ProbeConfig pc;
        pc.id        = (const char*)p["id"];
        pc.location  = (const char*)p["location"];
        a.probes.push_back(pc);
      }
    } else if (o.containsKey("probeId")) {
      // backward compatibility
      ProbeConfig pc;
      pc.id        = (const char*)o["probeId"];
      pc.location  = (const char*)o["location"];
      a.probes.push_back(pc);
    }

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
  File f = FSYSTEM.open(CONFIG_PATH, "w");
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

// ================================================================
void ConfigManager::toJson(JsonDocument& doc) {
  doc["statsIntervalMs"] = statsIntervalMs;
  JsonObject g = doc.createNestedObject("global");
  g["easterEgg"]        = global.easterEgg;
  g["aggregateMode"]    = String(global.aggregateMode);
  g["aggregateN"]       = global.aggregateN;
  g["diagPixelInterval"] = global.diagPixelInterval;


  JsonArray arr = doc.createNestedArray("areas");
  for (auto& a : _areas) {
    JsonObject o = arr.createNestedObject();
    o["name"]        = a.name;
    o["useBaseline"] = a.useBaseline;
    o["overrideMin"] = a.overrideMin;
    o["overrideMax"] = a.overrideMax;

    // --- probes array ---
    JsonArray pa = o.createNestedArray("probes");
    for (auto& p : a.probes) {
      JsonObject pj = pa.createNestedObject();
      pj["id"]        = p.id;
      pj["location"]  = p.location;
    }

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
    a.useBaseline = true;
    a.overrideMin = -1;
    a.overrideMax = -1;
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
  for (auto& a : _areas) {
    for (auto& p : a.probes) {
      if (p.id.equalsIgnoreCase(probe))
        return &a;
    }
  }
  return nullptr;
}


// ================================================================
bool ConfigManager::setProbe(const String& probe, const String& area, const String& loc) {
  AreaConfig* a = findAreaByName(area);
  if (!a) return false;

  // update existing if found
  for (auto& p : a->probes) {
    if (p.id.equalsIgnoreCase(probe)) {
      p.location = loc;
      return save();
    }
  }

  // otherwise add new
  ProbeConfig pc;
  pc.id = probe;
  pc.location = loc;
  a->probes.push_back(pc);
  return save();
}

bool ConfigManager::removeProbe(const String& probe) {
  for (auto& a : _areas) {
    for (size_t i = 0; i < a.probes.size(); ++i) {
      if (a.probes[i].id.equalsIgnoreCase(probe)) {
        a.probes.erase(a.probes.begin() + i);
        return save();
      }
    }
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

String ConfigManager::getConfigString() const {
  String s = "CONFIG ";
  s += "EE:" + String(global.easterEgg ? "T" : "F") + " ";
  s += "AGV:" + String(global.aggregateMode) + " ";
  s += "AGN:" + String(global.aggregateN) + " ";
  s += " LEDI:" + String(global.ledUpdateInterval);
  s += " DPI:"  + String(global.diagPixelInterval);

  return s;
}

bool ConfigManager::setConfigValue(const String& key, const String& val) {
  String k = key; k.toUpperCase();
  String v = val; v.toUpperCase();

  if (k == "EE") {
    global.easterEgg = (v == "T" || v == "1" || v == "ON" || v == "TRUE");
  }
  else if (k == "AGV") {
    if (v == "A" || v == "M") global.aggregateMode = v[0];
    else return false;
  }
  else if (k == "AGN") {
    int n = v.toInt();
    if (n < 1 || n > 10) return false;
    global.aggregateN = n;
  }
  else if (k == "LEDI") {
    int n = v.toInt();
    if (n < 100 || n > 60000) return false;
    global.ledUpdateInterval = n;
  }
  else if (k == "DPI") {
    int n = v.toInt();
    if (n < 1000 || n > 600000) return false;
    global.diagPixelInterval = n;
  }
  else return false;

  return save();
}

