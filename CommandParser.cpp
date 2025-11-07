#include "CommandParser.h"
#include "Baselines.h"

// ================================================================
//  Timestamped debug helpers
// ================================================================
static String nowStamp() {
  unsigned long ms = millis();
  unsigned long s = ms / 1000;
  unsigned long m = s / 60;
  char buf[20];
  snprintf(buf, sizeof(buf), "[%02lu:%02lu.%03lu] ", m % 60, s % 60, ms % 1000);
  return String(buf);
}
static void echoDiagTX(const String& msg) {
  Serial.print(nowStamp());
  Serial.print("[DIAG TX] ");
  Serial.println(msg);
}

// ================================================================
//  Small utilities
// ================================================================
static bool parseBoolToken(const String& s, bool& out) {
  if (s.equalsIgnoreCase("true") || s == "1") { out = true;  return true; }
  if (s.equalsIgnoreCase("false")|| s == "0") { out = false; return true; }
  return false;
}
static String up(const String& s){ String o=s; o.toUpperCase(); return o; }

// ================================================================
//  Handle incoming diagnostic UART commands
// ================================================================
void CommandParser::handleCommand(const String& raw) {
  String line = raw;
  line.trim();

  // Strip Meshtastic shortname prefix like "A4B2:"
  int colon = line.indexOf(':');
  if (colon > 0 && colon <= 6) {
    String prefix = line.substring(0, colon);
    if (!prefix.equalsIgnoreCase("SET") && !prefix.equalsIgnoreCase("GET")) {
      line = line.substring(colon + 1);
      line.trim();
      Serial.print(nowStamp());
      Serial.print("[DIAG CMD] Stripped prefix: ");
      Serial.println(prefix);
    }
  }

  if (line.length()) {
    Serial.print(nowStamp());
    Serial.print("[DIAG CMD] Parsed as: ");
    Serial.println(line);
  } else return;

  // === Command matching ===
  if (line.equalsIgnoreCase("GET AREAS")) { cmdGetAreas(); return; }
  if (line.startsWith("GET STATS")) { cmdGetStats(); return; }
  if (line.startsWith("GET THRESHOLD")) { cmdGetThreshold(); return; }
  if (line.equalsIgnoreCase("GET STATS_INTERVAL")) { cmdGetStatsInterval(); return; }

  if (line.startsWith("GET USE_BASELINE ")) {
    String area = line.substring(17); area.trim();
    cmdGetUseBaseline(area); return;
  }

  if (line.startsWith("SET USE_BASELINE ")) {
    String rest = line.substring(17); rest.trim();
    int sp = rest.lastIndexOf(' ');
    if (sp > 0) {
      String area = rest.substring(0, sp);
      String btok = rest.substring(sp+1);
      bool on=false; if (parseBoolToken(btok, on)) { cmdSetUseBaseline(area, on); return; }
    }
  }

  if (line.startsWith("SET STATS_INTERVAL ")) {
    unsigned long val = line.substring(19).toInt();
    if (val > 0) { cmdSetStatsInterval(val); return; }
  }

  if (line.startsWith("SET OVERRIDE ")) {
    String rest = line.substring(13);
    int sp1 = rest.lastIndexOf(' ');
    if (sp1>0) {
      float val = rest.substring(sp1+1).toFloat();
      String mid = rest.substring(0, sp1); mid.trim();
      int sp2 = mid.lastIndexOf(' ');
      if (sp2>0) {
        String minmax = mid.substring(sp2+1);
        String area   = mid.substring(0, sp2);
        bool isMin = minmax.equalsIgnoreCase("MIN");
        bool isMax = minmax.equalsIgnoreCase("MAX");
        if ((isMin||isMax) && area.length()) { cmdSetOverride(area, isMin, val); return; }
      }
    }
  }

  if (line.startsWith("SET THRESHOLD ")) {
    String rest = line.substring(14);
    int sp1 = rest.indexOf(' ');
    if (sp1>0) {
      String area = rest.substring(0, sp1);
      String rest2 = rest.substring(sp1+1); rest2.trim();
      int sp2 = rest2.indexOf(' ');
      int sp3 = rest2.lastIndexOf(' ');
      if (sp2>0 && sp3>sp2) {
        Metric m = metricFromString(rest2.substring(0, sp2));
        int pix  = rest2.substring(sp2+1, sp3).toInt();
        float v  = rest2.substring(sp3+1).toFloat();
        if (m != MET_COUNT && pix >=1 && pix<=6) { cmdSetThreshold(area, m, pix, v); return; }
      }
    }
  }

  if (line.startsWith("SET PROBES ")) {
    String rest = line.substring(11); rest.trim();
    int sp1 = rest.indexOf(' ');
    int sp2 = rest.indexOf(' ', sp1+1);
    if (sp1>0 && sp2>sp1) {
      String probe = rest.substring(0, sp1);
      String area  = rest.substring(sp1+1, sp2);
      String loc   = rest.substring(sp2+1);
      cmdSetProbes(probe, area, loc); return;
    }
  }

  if (line.startsWith("REMOVE PROBE ")) {
    String pid = line.substring(13); pid.trim();
    cmdRemoveProbe(pid); return;
  }

  String msg = String("ERR: Unrecognized command: ") + line;
  _out->println(msg);
  echoDiagTX(msg);
}

// ================================================================
//  Non-blocking STATS scheduler
// ================================================================
void CommandParser::processStatsJob() {
  if (!_statsJob.active) return;

  unsigned long now = millis();
  if (now - _statsJob.lastSent < _cfg->getStatsInterval()) return;

  if (_statsJob.areaIndex >= _cfg->areas().size()) {
    _statsJob.active = false;
    String msg = "STAT job complete";
    _out->println(msg);
    echoDiagTX(msg);
    return;
  }

  AreaConfig& a = _cfg->areas()[_statsJob.areaIndex];
  printStatLine(a, (Metric)_statsJob.metricIndex);
  _statsJob.lastSent = now;

  _statsJob.metricIndex++;
  if (_statsJob.metricIndex >= MET_COUNT) {
    _statsJob.metricIndex = 0;
    _statsJob.areaIndex++;
  }
}

// ================================================================
//  GET commands
// ================================================================
void CommandParser::cmdGetAreas() {
  for (auto& a : _cfg->areas()) {
    String msg = String("AREA: ") + a.name + " " + a.location + " " + a.probeId;
    _out->println(msg);
    echoDiagTX(msg);
  }
}

void CommandParser::cmdGetStats() {
  _statsJob.active = true;
  _statsJob.lastSent = 0;
  _statsJob.areaIndex = 0;
  _statsJob.metricIndex = 0;
  String msg = "STAT job started";
  _out->println(msg);
  echoDiagTX(msg);
}

void CommandParser::cmdGetThreshold() {
  for (auto& a : _cfg->areas()) {
    for (int m=0;m<MET_COUNT;m++) {
      String msg = String("THRESHOLD ") + a.name + " " + metricToString((Metric)m);
      for (int i=0;i<6;i++) { msg += " " + String(a.thresholds[m].values[i],2); }
      _out->println(msg);
      echoDiagTX(msg);
    }
  }
}

void CommandParser::cmdGetUseBaseline(const String& area) {
  bool b=false;
  String msg;
  if (_cfg->getUseBaseline(area, b)) {
    msg = String("USE_BASELINE ") + up(area) + " " + (b ? "True" : "False");
  } else {
    msg = String("USE_BASELINE ") + up(area) + " NOT_FOUND";
  }
  _out->println(msg);
  echoDiagTX(msg);
}

void CommandParser::cmdGetStatsInterval() {
  unsigned long val = _cfg->getStatsInterval();
  String msg = String("STATS_INTERVAL ") + String(val);
  _out->println(msg);
  echoDiagTX(msg);
}

// ================================================================
//  SET commands
// ================================================================
void CommandParser::cmdSetUseBaseline(const String& area, bool on) {
  String msg;
  if (_cfg->setUseBaseline(area, on)) {
    msg = String("USE_BASELINE ") + up(area) + " " + (on ? "True" : "False") + " ACCEPTED";
  } else {
    msg = String("USE_BASELINE ") + up(area) + " REJECTED";
  }
  _out->println(msg);
  echoDiagTX(msg);
}

void CommandParser::cmdSetOverride(const String& area, bool isMin, float val) {
  String msg;
  if (_cfg->setOverride(area, isMin, val)) {
    msg = String("OVERRIDE ") + up(area) + " " + (isMin ? "MIN " : "MAX ") + String(val,2) + " ACCEPTED";
  } else {
    msg = String("OVERRIDE ") + up(area) + " REJECTED";
  }
  _out->println(msg);
  echoDiagTX(msg);
}

void CommandParser::cmdSetThreshold(const String& area, Metric m, int pix, float val) {
  String msg;
  if (_cfg->setThreshold(area, m, pix, val)) {
    msg = String("THRESHOLD ") + up(area) + " " + metricToString(m) + " " +
          String(pix) + " " + String(val,2) + " ACCEPTED";
  } else {
    msg = String("THRESHOLD ") + up(area) + " REJECTED";
  }
  _out->println(msg);
  echoDiagTX(msg);
}

void CommandParser::cmdSetProbes(const String& probe, const String& area, const String& loc) {
  String msg;
  if (_cfg->setProbe(probe, area, loc)) {
    msg = String("PROBES ") + up(probe) + " " + up(area) + " " + up(loc) + " ACCEPTED";
  } else {
    msg = String("PROBES ") + up(probe) + " " + up(area) + " " + up(loc) + " REJECTED";
  }
  _out->println(msg);
  echoDiagTX(msg);
}

void CommandParser::cmdRemoveProbe(const String& probe) {
  String msg;
  if (_cfg->removeProbe(probe)) {
    msg = String("PROBE ") + up(probe) + " REMOVED";
  } else {
    msg = String("PROBE ") + up(probe) + " NOT_FOUND";
  }
  _out->println(msg);
  echoDiagTX(msg);
}

void CommandParser::cmdSetStatsInterval(unsigned long val) {
  String msg;
  if (_cfg->setStatsInterval(val)) {
    msg = String("STATS_INTERVAL SET TO ") + String(val) + " ACCEPTED";
  } else {
    msg = "STATS_INTERVAL REJECTED (valid range 1000–60000)";
  }
  _out->println(msg);
  echoDiagTX(msg);
}

// ================================================================
//  printStatLine helper
// ================================================================
static void chooseMinMaxForStat(const AreaConfig& a, Metric m, float& outMin, float& outMax, bool& usedBaseline) {
  usedBaseline = false;
  bool haveLive = a.rt.inited[m];
  float bMin=0,bMax=0;
  switch (m) {
    case MET_CO2:  bMin=BASELINE_CO2_MIN; bMax=BASELINE_CO2_MAX; break;
    case MET_TEMP: bMin=BASELINE_TEMP_MIN; bMax=BASELINE_TEMP_MAX; break;
    case MET_HUM:  bMin=BASELINE_HUM_MIN;  bMax=BASELINE_HUM_MAX;  break;
    case MET_DB:   bMin=BASELINE_DB_MIN;   bMax=BASELINE_DB_MAX;   break;
    default: break;
  }
  if (a.useBaseline) {
    if (haveLive && a.rt.liveMax[m] >= (BASELINE_NEAR_FRACTION * bMax)) {
      outMin = a.rt.liveMin[m]; outMax = a.rt.liveMax[m];
      usedBaseline = true; return;
    }
    outMin = bMin; outMax = bMax; usedBaseline = true;
  } else if (haveLive) {
    outMin = a.rt.liveMin[m]; outMax = a.rt.liveMax[m];
  } else {
    outMin = bMin; outMax = bMax; usedBaseline = false;
  }
}

void CommandParser::printStatLine(const AreaConfig& a, Metric m) {
  float mn=0,mx=0; bool usedBase=false;
  chooseMinMaxForStat(a, m, mn, mx, usedBase);
  String msg = String("STAT: ") + a.name + " " + metricToString(m) +
    " min:" + String(mn,2) + " max:" + String(mx,2) +
    " min_o:" + String(a.overrideMin,2) + " max_o:" + String(a.overrideMax,2) +
    " baseline:" + (a.useBaseline ? "true" : "false");
  _out->println(msg);
  echoDiagTX(msg);
}
