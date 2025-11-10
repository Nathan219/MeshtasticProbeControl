#include "SensorHandler.h"
#include "Metrics.h"
#include "ConfigManager.h"
#include <math.h>

// ================================================================
// Multi-probe aware average/max computation for a metric in an area
// ================================================================


// Uncomment this to see LED aggregation debug logs over USB
// #define DEBUG_LED_AGG 1

// from CommandParser.cpp for unified timestamped output
void echoDiagTX(const String& msg);

// ================================================================
//  Handle incoming sensor messages
// ================================================================
void SensorHandler::handleSensorMessage(const String& line) {
  int colon = line.indexOf(':');
  if (colon < 0) return;

  String probeId = line.substring(0, colon);
  probeId.trim();
  probeId.toLowerCase(); // normalize for lookups

  String payload = line.substring(colon + 1);
  payload.trim();

  // --- Handle SET PROBE command from probe UART ---
  if (payload.startsWith("SET PROBE")) {
    String rest = payload.substring(9);
    rest.trim();
    int sp = rest.indexOf(' ');
    if (sp < 0) {
      String err = "ERR: Bad SET PROBE syntax";
      if (_sensorSerial) _sensorSerial->println(err);
      _out->println(err);
      echoDiagTX(err);
      return;
    }

    String area = rest.substring(0, sp);
    String loc  = rest.substring(sp + 1);
    area.trim();
    loc.trim();

    if (_cfg->setProbe(probeId, area, loc)) {
      if (_cfg->save()) {
        String msg = "PROBE " + probeId + " " + area + " " + loc + " ACCEPTED";
        if (_sensorSerial) _sensorSerial->println(msg);   // send ACK to sensors UART
        _out->println(msg);                               // show on USB serial
        echoDiagTX(msg);                                  // show on diag serial
      } else {
        String msg = "ERR: Failed to save config after setting probe " + probeId;
        if (_sensorSerial) _sensorSerial->println(msg);
        _out->println(msg);
        echoDiagTX(msg);
      }
    } else {
      String msg = "ERR: Failed to set probe " + probeId;
      if (_sensorSerial) _sensorSerial->println(msg);
      _out->println(msg);
      echoDiagTX(msg);
    }
    return; // handled, don't parse as sensor data
  }

  // --- Otherwise, expect sensor data like "CO2:451,Temp:26.7,Hum:58.7,Sound:45" ---
  float co2 = NAN, temp = NAN, hum = NAN, db = NAN;

  int idx = 0;
  while (idx < payload.length()) {
    int comma = payload.indexOf(',', idx);
    String token = (comma == -1) ? payload.substring(idx) : payload.substring(idx, comma);
    token.trim();

    int sep = token.indexOf(':');
    if (sep > 0) {
      String key = token.substring(0, sep);
      String val = token.substring(sep + 1);
      key.trim(); val.trim();
      float fval = val.toFloat();

      if (key.equalsIgnoreCase("CO2"))   co2  = fval;
      else if (key.equalsIgnoreCase("TEMP")) temp = fval;
      else if (key.equalsIgnoreCase("HUM"))  hum  = fval;
      else if (key.equalsIgnoreCase("SOUND") || key.equalsIgnoreCase("DB")) db = fval;
    }
    if (comma == -1) break;
    idx = comma + 1;
  }

  if (!isnan(co2)) updateHistory(probeId, MET_CO2, co2);
  if (!isnan(temp)) updateHistory(probeId, MET_TEMP, temp);
  if (!isnan(hum))  updateHistory(probeId, MET_HUM, hum);
  if (!isnan(db))   updateHistory(probeId, MET_DB, db);

  String updateMsg = probeId + " updated";
  _out->println("[DATA]" + updateMsg);
}

// ================================================================
//  History management
// ================================================================
void SensorHandler::updateHistory(const String& probe, Metric m, float value) {
  _history[probe][m].push_back(value);
  if (_history[probe][m].size() > 10)
    _history[probe][m].erase(_history[probe][m].begin());

  updateAreaStats(probe, m, value);
}

// ================================================================
//  Update area min/max based on incoming values
// ================================================================
void SensorHandler::updateAreaStats(const String& probe, Metric m, float value) {
  AreaConfig* area = _cfg->findAreaByProbe(probe);
  if (!area) return;

  if (!area->rt.inited[m]) {
    area->rt.liveMin[m] = value;
    area->rt.liveMax[m] = value;
    area->rt.inited[m]  = true;
  } else {
    if (value < area->rt.liveMin[m]) area->rt.liveMin[m] = value;
    if (value > area->rt.liveMax[m]) area->rt.liveMax[m] = value;
  }

  _cfg->save();
}

// ================================================================
//  History accessors
// ================================================================
bool SensorHandler::getHistory(const String& probe, Metric m, std::vector<float>& out) {
  String key = probe; key.toLowerCase();
  auto itProbe = _history.find(key);
  if (itProbe == _history.end()) return false;
  auto itMetric = itProbe->second.find(m);
  if (itMetric == itProbe->second.end()) return false;
  out = itMetric->second;
  return true;
}

bool SensorHandler::getAllHistory(const String& probe, std::vector<std::pair<Metric,std::vector<float>>>& out) {
  String key = probe; key.toLowerCase();
  auto itProbe = _history.find(key);
  if (itProbe == _history.end()) return false;
  out.clear();
  for (auto& kv : itProbe->second) {
    out.push_back(kv);
  }
  return true;
}

// ================================================================
// Return the recent average (or max) of the last N values for metric m
// across all probes assigned to the given area.
float SensorHandler::areaMetricRecentAvg(const String& areaName, Metric m, int N) {
  if (!_cfg) return NAN;
  AreaConfig* a = _cfg->findAreaByName(areaName);
  if (!a) return NAN;
  if (a->probes.empty()) return NAN;

  char aggMode = _cfg->global.aggregateMode;  // 'A' or 'M'
  if (aggMode != 'A' && aggMode != 'M') aggMode = 'A';
  if (N < 1) N = 1;
  if (N > 10) N = 10;

  float total = 0;
  int count = 0;
  float best = -INFINITY;

#ifdef DEBUG_LED_AGG
  if (_out) {
    _out->println("[DEBUG_LED_AGG] Area: " + areaName + " Metric: " + metricToString(m));
    _out->print("  Mode: "); _out->print(aggMode == 'A' ? "AVG" : "MAX");
    _out->print("  Window: "); _out->println(N);
  }
#endif

  for (auto& p : a->probes) {
    auto it = _history.find(p.id);
    if (it == _history.end()) continue;

    const auto& probeData = it->second;
    auto hit = probeData.find(m);
    if (hit == probeData.end()) continue;

    const auto& vals = hit->second;
    if (vals.empty()) continue;

    float sum = 0;
    int used = 0;
    for (int i = vals.size() - 1; i >= 0 && used < N; --i) {
      sum += vals[i];
      used++;
    }
    if (used == 0) continue;
    float avg = sum / used;

#ifdef DEBUG_LED_AGG
    if (_out) {
      _out->print("    Probe "); _out->print(p.id);
      _out->print(" avg("); _out->print(used); _out->print("): ");
      _out->println(avg, 2);
    }
#endif

    if (aggMode == 'A') {
      total += avg;
      count++;
    } else if (aggMode == 'M') {
      if (avg > best) best = avg;
    }
  }

  float result = NAN;
  if (aggMode == 'A') {
    if (count > 0) result = total / count;
  } else {
    if (best != -INFINITY) result = best;
  }

#ifdef DEBUG_LED_AGG
  if (_out) {
    _out->print("  Result: ");
    if (isnan(result)) _out->println("NAN");
    else _out->println(result, 2);
  }
#endif

  return result;
}
