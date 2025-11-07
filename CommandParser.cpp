#include "CommandParser.h"
#include "Metrics.h"

#define MESSAGE_DELAY     2000

void CommandParser::sendDiag(const String& msg) {
  if (_diag){
    _diag->flush();
    _diag->println(msg);
    _diag->flush();
  }
  delay(MESSAGE_DELAY);
}

void CommandParser::sendUSB(const String& msg) {
  if (_usb) _usb->println("[DIAG TX] " + msg);
}

// ================================================================
//  handleCommand
// ================================================================
void CommandParser::handleCommand(const String& line)  {
  String cmd = line;   // make a local, mutable copy
  cmd.trim();

  // ✅ Strip Meshtastic short-name prefix (like "ABCD:" or "💧7:")
  int colonPos = cmd.indexOf(':');
  if (colonPos != -1 && colonPos <= 4) {
    cmd = cmd.substring(colonPos + 1);
    cmd.trim();
  }

  cmd.replace("\r", "");
  cmd.replace("\n", "");


  // --------------------------------------------------------------
  // GET STATS
  // --------------------------------------------------------------
  if (cmd.equalsIgnoreCase("GET STATS")) {
    if (_statsJob.active) {
      sendUSB("ERR: Stats job already running");
      return;
    }

    _statsJob.active = true;
    _statsJob.areaIndex = 0;
    _statsJob.metricIndex = 0;
    _statsJob.lastSend = 0;
    sendUSB("GET STATS started (1s interval)");
    return;
  }

  // --------------------------------------------------------------
  // GET STATS {AREA}
  // --------------------------------------------------------------
  if (cmd.startsWith("GET STATS ")) {
    String areaName = cmd.substring(10);
    areaName.trim();
    AreaConfig* a = _cfg->findAreaByName(areaName);
    if (!a) {
      sendDiag("ERR: Unknown area " + areaName);
      sendUSB("ERR: Unknown area " + areaName);
      return;
    }
    for (int i = 0; i < MET_COUNT; i++) {
      printStatLine(*a, static_cast<Metric>(i));
      // delay(MESSAGE_DELAY);
    }
    return;
  }

  // --------------------------------------------------------------
  // GET AREAS
  // --------------------------------------------------------------
  if (cmd.equalsIgnoreCase("GET AREAS")) {
    for (auto& a : _cfg->areas()) {
      String msg = "AREA: " + a.name;
      if (a.location.length()) msg += " " + a.location;
      if (a.probeId.length()) msg += " " + a.probeId;
      sendDiag(msg);
      sendUSB(msg);
      // delay(MESSAGE_DELAY);
    }
    return;
  }

  // --------------------------------------------------------------
  // REMOVE PROBE {ID}
  // --------------------------------------------------------------
  if (cmd.startsWith("REMOVE PROBE ")) {
    String probeId = cmd.substring(13);
    probeId.trim();
    if (_cfg->removeProbe(probeId)) {
      String msg = "PROBE " + probeId + " REMOVED";
      sendDiag(msg);
      sendUSB(msg);
    } else {
      sendDiag("ERR: Probe not found " + probeId);
      sendUSB("ERR: Probe not found " + probeId);
    }
    return;
  }

  // ==========================================
  // GET THRESHOLDS {AREA_NAME}
  // ==========================================
  if (cmd.startsWith("GET THRESHOLDS")) {
    String area = cmd.substring(14);
    area.trim();
    if (area.isEmpty()) {
      sendDiag("ERR: Missing area name");
      return;
    }

    AreaConfig* a = _cfg->findAreaByName(area);
    if (!a) {
      sendDiag("ERR: Unknown area " + area);
      return;
    }

    for (int mi = 0; mi < MET_COUNT; mi++) {
      Metric m = static_cast<Metric>(mi);
      String msg = "THRESHOLD " + a->name + " " + metricToString(m);
      for (int i = 0; i < 6; i++)
        msg += " " + String(a->thresholds[m].values[i], 2);

      sendDiag(msg);
      sendUSB(msg);
      delay(50); // small pacing delay to prevent overflow
    }
    return;
  }
  
  auto interpretThreshold = [](float v) -> String {
    if (v < 0) return "unused";
    if (v < 1.0f) return String(v * 100.0f, 1) + "%";
    return String(v, 1);
  };

  if (cmd.startsWith("SET THRESHOLD")) {
    String rest = cmd.substring(13);
    rest.trim();

    int sp1 = rest.indexOf(' ');
    if (sp1 < 0) { sendDiag("ERR: Missing area"); sendUSB("ERR: Missing area"); return; }

    String area = rest.substring(0, sp1);
    String r2 = rest.substring(sp1 + 1);
    r2.trim();

    int sp2 = r2.indexOf(' ');
    if (sp2 < 0) { sendDiag("ERR: Missing metric"); sendUSB("ERR: Missing metric"); return; }

    String met = r2.substring(0, sp2);
    Metric m = metricFromString(met);
    if (m == MET_INVALID) { sendDiag("ERR: Invalid metric"); sendUSB("ERR: Invalid metric"); return; }

    String r3 = r2.substring(sp2 + 1);
    r3.trim();

    // --- Multi-value form (comma separated) ---
    if (r3.indexOf(',') >= 0) {
      float vals[6] = {-1, -1, -1, -1, -1, -1};
      int count = 0;

      while (r3.length() > 0 && count < 6) {
        int comma = r3.indexOf(',');
        String tok = (comma == -1) ? r3 : r3.substring(0, comma);
        tok.trim();
        float v = tok.toFloat();
        vals[count++] = v;
        if (comma == -1) break;
        r3 = r3.substring(comma + 1);
        r3.trim();
      }

      for (int i = 0; i < count; i++) _cfg->setThreshold(area, m, i + 1, vals[i]);
      _cfg->save();

      String msg = "THRESHOLD " + area + " " + metricToString(m) + " [";
      for (int i = 0; i < count; i++) {
        if (i) msg += ", ";
        msg += interpretThreshold(vals[i]);
      }
      msg += "] ACCEPTED";
      sendDiag(msg);
      sendUSB(msg);
      return;
    }

    // --- Single pixel/value form ---
    int sp3 = r3.indexOf(' ');
    if (sp3 < 0) {
      sendDiag("ERR: Missing pixel/value");
      sendUSB("ERR: Missing pixel/value");
      return;
    }

    int pix = r3.substring(0, sp3).toInt();
    float val = r3.substring(sp3 + 1).toFloat();

    if (!_cfg->setThreshold(area, m, pix, val)) {
      sendDiag("ERR: Failed to set threshold");
      sendUSB("ERR: Failed to set threshold");
      return;
    }

    _cfg->save();

    String msg = "THRESHOLD " + area + " " + metricToString(m) +
                " " + String(pix) + " " + interpretThreshold(val) + " ACCEPTED";
    sendDiag(msg);
    sendUSB(msg);
    return;
  }



  // --------------------------------------------------------------
  // GET HISTORY {PROBE}
  // --------------------------------------------------------------
  if (cmd.startsWith("GET HISTORY")) {
    String arg = cmd.substring(11);
    arg.trim();
    if (arg.isEmpty()) {
      sendDiag("ERR: Missing probe id");
      sendUSB("ERR: Missing probe id");
      return;
    }

    std::vector<std::pair<Metric, std::vector<float>>> data;
    if (_sensors->getAllHistory(arg, data)) {
      for (auto& kv : data) {
        String msg = "HIST " + arg + " " + metricToString(kv.first) + ":";
        for (float v : kv.second) msg += " " + String(v, 1);
        sendDiag(msg);
        sendUSB(msg);
        delay(50);
      }
    } else {
      sendDiag("ERR: No history for " + arg);
      sendUSB("ERR: No history for " + arg);
    }
    return;
  }

  // --------------------------------------------------------------
  // SET USE_BASELINE {AREA} {True/False}
  // --------------------------------------------------------------
  if (cmd.startsWith("SET USE_BASELINE ")) {
    String rest = cmd.substring(17);
    rest.trim();
    int sp = rest.indexOf(' ');
    if (sp < 0) return;
    String area = rest.substring(0, sp);
    String valStr = rest.substring(sp + 1);
    bool flag = valStr.equalsIgnoreCase("1") || valStr.equalsIgnoreCase("true");
    _cfg->setUseBaseline(area, flag);
    String msg = "USE_BASELINE " + area + " " + String(flag ? "True" : "False") + " ACCEPTED";
    sendDiag(msg);
    sendUSB(msg);
    return;
  }

  // --------------------------------------------------------------
  // GET USE_BASELINE {AREA}
  // --------------------------------------------------------------
  if (cmd.startsWith("GET USE_BASELINE ")) {
    String area = cmd.substring(17);
    area.trim();
    bool flag = false;
    _cfg->getUseBaseline(area, flag);
    String msg = "USE_BASELINE " + area + " " + String(flag ? "True" : "False");
    sendDiag(msg);
    sendUSB(msg);
    return;
  }

  sendDiag("ERR: Unrecognized command: " + cmd);
  sendUSB("ERR: Unrecognized command: " + cmd);
}

// ================================================================
//  processStatsJob
// ================================================================
void CommandParser::processStatsJob() {
  if (!_statsJob.active) return;

  unsigned long now = millis();
  const unsigned long delayMs = 1000;  // 1s between each STAT

  if (now - _statsJob.lastSend < delayMs) return;
  _statsJob.lastSend = now;

  auto& areas = _cfg->areas();
  if (_statsJob.areaIndex >= areas.size()) {
    _statsJob.active = false;
    sendUSB("GET STATS done.");
    return;
  }

  AreaConfig& a = areas[_statsJob.areaIndex];
  Metric m = static_cast<Metric>(_statsJob.metricIndex);
  printStatLine(a, m);

  _statsJob.metricIndex++;
  if (_statsJob.metricIndex >= MET_COUNT) {
    _statsJob.metricIndex = 0;
    _statsJob.areaIndex++;
  }
}

// ================================================================
//  printStatLine
// ================================================================
void CommandParser::printStatLine(const AreaConfig& a, Metric m) {
  String msg = "STAT: " + a.name + " " + metricToString(m) +
               " min:" + String(a.rt.liveMin[m], 1) +
               " max:" + String(a.rt.liveMax[m], 1) +
               " min_o:" + String(a.overrideMin, 1) +
               " max_o:" + String(a.overrideMax, 1);

  sendDiag(msg);
  sendUSB(msg);
}
