#include "CommandParser.h"
#include "Metrics.h"
#include "LedMessenger.h"

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
    auto& areas = _cfg->areas();
    for (auto& a : areas) {
      if (a.probes.empty()) {
        String msg = "AREA: " + a.name + " (no probes)";
        sendDiag(msg);
        sendUSB(msg);
        continue;
      }
      for (auto& p : a.probes) {
        String msg = "AREA: " + a.name + " " + p.location + " " + p.id;
        sendDiag(msg);
        sendUSB(msg);
        delay(100);  // small pacing delay so Meshtastic doesn't drop lines
      }
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
  // GET PIXELS
  // ==========================================
  if (cmd.equalsIgnoreCase("GET PIXELS")) {
    auto& areas = _cfg->areas();
    for (auto& a : areas) {
      String msg = "PIXELS " + a.name + " " + String(a.rt.lastPixel);
      sendUSB(msg);
      sendDiag(msg);
    }
    return;
  }

  // ==========================================
  // SET PROBES {PROBE_ID} {AREA} {LOCATION}
  // ==========================================
  if (cmd.startsWith("SET PROBES")) {
    String rest = cmd.substring(10);
    rest.trim();

    int sp1 = rest.indexOf(' ');
    if (sp1 < 0) {
      sendDiag("ERR: Missing probe id");
      sendUSB("ERR: Missing probe id");
      return;
    }

    String probeId = rest.substring(0, sp1);
    probeId.trim();
    probeId.toLowerCase();

    String r2 = rest.substring(sp1 + 1);
    r2.trim();
    int sp2 = r2.indexOf(' ');
    if (sp2 < 0) {
      sendDiag("ERR: Missing area");
      sendUSB("ERR: Missing area");
      return;
    }

    String area = r2.substring(0, sp2);
    area.trim();

    String loc = r2.substring(sp2 + 1);
    loc.trim();
    if (loc.isEmpty()) {
      sendDiag("ERR: Missing location");
      sendUSB("ERR: Missing location");
      return;
    }

    if (_cfg->setProbe(probeId, area, loc)) {
      String msg = "PROBE " + probeId + " " + area + " " + loc + " ACCEPTED";
      sendDiag(msg);
      sendUSB(msg);
    } else {
      String msg = "ERR: Failed to set probe " + probeId;
      sendDiag(msg);
      sendUSB(msg);
    }
    return;
  }

  // ==========================================
  // GET PEOPLW
  // ==========================================
  if (cmd.equalsIgnoreCase("GET PEOPLE")) {
    auto& areas = _cfg->areas();
    for (auto& a : areas) {
      String msg = "PEOPLE " + a.name + " " + String(a.rt.lastPixel);
      sendUSB(msg);
      sendDiag(msg);
    }
  }
  if (cmd.startsWith("GET PEOPLE")) {
    String arg = cmd.substring(11);
    arg.trim();
    auto& areas = _cfg->areas();

    for (auto& a : areas) {
      if (arg.length() > 0 && !a.name.equalsIgnoreCase(arg)) continue;
      String msg = "PEOPLE " + a.name + " " + String(a.rt.lastPixel);
      if (arg.equalsIgnoreCase("VERBOSE")) {
        msg += " (min=" + String(a.rt.liveMin[MET_CO2], 1) + ", max=" + String(a.rt.liveMax[MET_CO2], 1) + ")";
      }
      sendUSB(msg);
      sendDiag(msg);
    }
  }

  // ==========================================
  // GET THRESHOLDS {AREA_NAME} [METRIC]
  // If METRIC is provided, returns only that metric's thresholds
  // If METRIC is omitted, returns all metrics' thresholds
  // ==========================================
  if (cmd.startsWith("GET THRESHOLDS")) {
    String rest = cmd.substring(14);  // Skip "GET THRESHOLDS " (14 chars)
    rest.trim();
    
    if (rest.isEmpty()) {
      sendDiag("ERR: Missing area name");
      sendUSB("ERR: Missing area name");
      return;
    }

    // Find the first space to separate area from metric (if metric is provided)
    int sp1 = rest.indexOf(' ');
    String area;
    String met;
    bool hasMetric = (sp1 >= 0);

    if (hasMetric) {
      // Both area and metric provided
      area = rest.substring(0, sp1);
      met = rest.substring(sp1 + 1);
      met.trim();
    } else {
      // Only area provided - will return all metrics
      area = rest;
    }

    AreaConfig* a = _cfg->findAreaByName(area);
    if (!a) {
      sendDiag("ERR: Unknown area " + area);
      sendUSB("ERR: Unknown area " + area);
      return;
    }

    if (hasMetric) {
      // Return thresholds for the specified metric only
      Metric m = metricFromString(met);
      if (m == MET_INVALID) {
        sendDiag("ERR: Invalid metric");
        sendUSB("ERR: Invalid metric");
        return;
      }

      String msg = "THRESHOLD " + a->name + " " + metricToString(m);
      for (int i = 0; i < 6; i++)
        msg += " " + String(a->thresholds[m].values[i], 2);

      // Send separately
      sendDiag(msg);
      sendUSB(msg);
    } else {
      // Return thresholds for all metrics - send each one separately
      for (int mi = 0; mi < MET_COUNT; mi++) {
        Metric m = static_cast<Metric>(mi);
        String msg = "THRESHOLD " + a->name + " " + metricToString(m);
        for (int i = 0; i < 6; i++)
          msg += " " + String(a->thresholds[m].values[i], 2);

        // Send each message separately with proper delays
        sendDiag(msg);
        sendUSB(msg);
        // sendDiag already includes MESSAGE_DELAY, so each message is sent separately
      }
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

  if (cmd.equalsIgnoreCase("GET CONFIG")) {
    cmdGetConfig();
    return;
  }
  if (cmd.startsWith("SET CONFIG")) {
    String args = cmd.substring(10);
    args.trim();
    cmdSetConfig(args);
    return;
  }

  // ==========================================
  // LED {MESSAGE}
  // Forwards message to LED controller and returns response
  // ==========================================
  if (cmd.startsWith("LED ")) {
    if (!_leds) {
      sendDiag("ERR: LED controller not available");
      sendUSB("ERR: LED controller not available");
      return;
    }
    
    String message = cmd.substring(4);  // Skip "LED "
    message.trim();
    
    if (message.isEmpty()) {
      sendDiag("ERR: Missing LED message");
      sendUSB("ERR: Missing LED message");
      return;
    }
    
    // Send message to LED controller and get response
    String response = _leds->sendLedWithResponse(message);
    
    // Format response (if empty, indicate no response received)
    String result = "ACCEPTED LED -> " + (response.length() > 0 ? response : "(no response)");
    sendDiag(result);
    sendUSB(result);
    return;
  }

  // ==========================================
  // SET TESTMODE {TRUE/FALSE}
  // Enables/disables test mode (cycles pixels 0-6 every 5 seconds)
  // ==========================================
  if (cmd.startsWith("SET TESTMODE ")) {
    if (!_leds) {
      sendDiag("ERR: LED controller not available");
      sendUSB("ERR: LED controller not available");
      return;
    }
    
    String value = cmd.substring(13);  // Skip "SET TESTMODE "
    value.trim();
    value.toUpperCase();
    
    bool enabled = (value == "TRUE" || value == "1" || value == "ON");
    _leds->setTestMode(enabled);
    
    String msg = "TESTMODE ACCEPTED";
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

void CommandParser::cmdGetConfig() {
  sendDiag(_cfg->getConfigString());
}

void CommandParser::cmdSetConfig(const String& args) {
  if (args.length() == 0) {
    sendDiag("ERR: No config provided");
    return;
  }

  // Example: "EE T, AGV M, AGN 5"
  int last = 0;
  int comma;
  bool ok = true;
  String applied = "CONFIG ";

  String s = args + ","; // sentinel
  while ((comma = s.indexOf(',', last)) != -1) {
    String pair = s.substring(last, comma);
    last = comma + 1;
    pair.trim();
    if (pair.length() == 0) continue;

    int sp = pair.indexOf(' ');
    if (sp < 0) { ok = false; continue; }

    String key = pair.substring(0, sp);
    String val = pair.substring(sp + 1);
    key.trim(); val.trim();

    if (_cfg->setConfigValue(key, val)) {
      applied += key + ":" + val + " ";
    } else {
      ok = false;
    }
  }

  if (ok)
    sendDiag(applied + "Accepted");
  else
    sendDiag("ERR: Invalid CONFIG parameters");
}

