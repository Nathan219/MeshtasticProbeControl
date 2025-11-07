#include "SensorHandler.h"
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

static void echoSensTX(const String& msg) {
  Serial.print(nowStamp());
  Serial.print("[SENS TX] ");
  Serial.println(msg);
}

static void echoDiagTX(const String& msg) {
  Serial.print(nowStamp());
  Serial.print("[DIAG TX] ");
  Serial.println(msg);
}

static String up(const String& s){ String o=s; o.toUpperCase(); return o; }

// ================================================================
//  Implementation
// ================================================================
ProbeRing* SensorHandler::upsertRing(const String& id) {
  String uid = up(id);
  for (int i=0;i<_probeCount;i++) if (_probes[i].id == uid) return &_probes[i].ring;
  if (_probeCount < (int)(sizeof(_probes)/sizeof(_probes[0]))) {
    _probes[_probeCount].id = uid;
    return &_probes[_probeCount++].ring;
  }
  _probes[0].id = uid;
  return &_probes[0].ring;
}

bool SensorHandler::parseTelemetry(const String& body, float& co2, float& t, float& h, float& d) {
  co2=t=h=d=NAN;
  int start=0;
  while (start < body.length()) {
    int comma = body.indexOf(',', start);
    String token = (comma<0) ? body.substring(start) : body.substring(start, comma);
    token.trim();
    int colon = token.indexOf(':');
    if (colon>0) {
      String key = token.substring(0, colon);
      String val = token.substring(colon+1);
      key.trim(); val.trim();
      float fv = val.toFloat();
      if (key.equalsIgnoreCase("CO2")) co2=fv;
      else if (key.equalsIgnoreCase("TEMP")) t=fv;
      else if (key.equalsIgnoreCase("HUM")) h=fv;
      else if (key.equalsIgnoreCase("SOUND") || key.equalsIgnoreCase("DB")) d=fv;
    }
    if (comma<0) break;
    start = comma+1;
  }
  return !(isnan(co2)&&isnan(t)&&isnan(h)&&isnan(d));
}

void SensorHandler::updateAreaLive(const String& probeId, float co2, float t, float h, float d) {
  AreaConfig* area = _cfg->findAreaByProbe(probeId);
  if (!area) return;
  float vals[MET_COUNT] = {co2,t,h,d};
  for (int m=0;m<MET_COUNT;m++) {
    float v = vals[m];
    if (isnan(v)) continue;
    if (!area->rt.inited[m]) {
      area->rt.inited[m]=true; area->rt.liveMin[m]=v; area->rt.liveMax[m]=v;
    } else {
      if (v < area->rt.liveMin[m]) area->rt.liveMin[m]=v;
      if (v > area->rt.liveMax[m]) area->rt.liveMax[m]=v;
    }
  }
}

void SensorHandler::ackSensor(const String& s) {
  if (_sensorSerial) {
    _sensorSerial->println(s);
    echoSensTX(s);
  }
}

void SensorHandler::handleProbeAssignFromProbe(const String& probeId, const String& area, const String& loc) {
  bool ok = _cfg->setProbe(up(probeId), up(area), up(loc));
  String msg = String("PROBE ") + up(probeId) + " " + up(area) + " " + up(loc) + (ok ? " ACCEPTED" : " REJECTED");
  ackSensor(msg);
}

void SensorHandler::handleSetProbes(const String& probeId, const String& area, const String& loc) {
  bool ok = _cfg->setProbe(up(probeId), up(area), up(loc));
  String msg = String("PROBES ") + up(probeId) + " " + up(area) + " " + up(loc) + (ok ? " ACCEPTED" : " REJECTED");
  ackSensor(msg);
  if (_diagSerial) {
    _diagSerial->println(msg);
    echoDiagTX(msg);
  }
}

void SensorHandler::handleRemoveProbe(const String& probeId) {
  bool ok = _cfg->removeProbe(up(probeId));
  String msg = String("PROBE ") + up(probeId) + (ok ? " REMOVED" : " NOT_FOUND");
  ackSensor(msg);
  if (_diagSerial) {
    _diagSerial->println(msg);
    echoDiagTX(msg);
  }
}

void SensorHandler::handleSensorLine(const String& lineIn) {
  if (!lineIn.length()) return;
  String line = lineIn; line.trim();

  // Mirror incoming sensor UART messages to USB
  Serial.print(nowStamp());
  Serial.print("[SENS RX] ");
  Serial.println(line);

  // Accept "SET PROBES ..." from Sensor UART too
  if (line.startsWith("SET PROBES ") || line.startsWith("set probes ") || line.startsWith("Set Probes ")) {
    String rest = line.substring(line.indexOf(' ') + 1); // after SET
    int sp1 = rest.indexOf(' '); if (sp1 < 0) return;
    String rest2 = rest.substring(sp1+1); rest2.trim(); // after "PROBES "
    int spA = rest2.indexOf(' ');
    int spB = rest2.indexOf(' ', spA+1);
    if (spA < 0 || spB < 0) return;
    String probe = rest2.substring(0, spA);
    String area  = rest2.substring(spA+1, spB);
    String loc   = rest2.substring(spB+1);
    handleSetProbes(probe, area, loc);
    return;
  }

  // Accept "REMOVE PROBE {PROBE_ID}"
  if (line.startsWith("REMOVE PROBE ") || line.startsWith("remove probe ") || line.startsWith("Remove Probe ")) {
    String pid = line.substring(line.lastIndexOf(' ')+1);
    pid.trim();
    handleRemoveProbe(pid);
    return;
  }

  // Probe self-assignment: "{PROBE_ID} SET PROBE AREA LOCATION"
  int sp1 = line.indexOf(' ');
  if (sp1 > 0) {
    String first = line.substring(0, sp1);
    int setIdx = line.indexOf(" SET PROBE ");
    if (setIdx == (int)first.length()) {
      String rest = line.substring(setIdx + 11);
      rest.trim();
      int sp = rest.indexOf(' ');
      if (sp > 0) {
        String area = rest.substring(0, sp);
        String loc  = rest.substring(sp + 1);
        handleProbeAssignFromProbe(first, area, loc);
        return;
      }
    }
  }

  // Telemetry: "{PROBE_ID}: CO2:...,Temp:...,Hum:...,Sound:..."
  int colon = line.indexOf(':');
  if (colon > 0) {
    String probeId = line.substring(0, colon);
    String payload = line.substring(colon + 1);
    payload.trim();
    float co2, t, h, d;
    if (parseTelemetry(payload, co2, t, h, d)) {
      ProbeRing* r = upsertRing(probeId);
      r->push(isnan(co2)?0:co2, isnan(t)?0:t, isnan(h)?0:h, isnan(d)?0:d, millis());
      updateAreaLive(up(probeId), co2, t, h, d);
    }
    return;
  }

  // Unknown line: mirror to diagnostics for visibility
  if (_diagSerial) {
    _diagSerial->println(String("UNPARSED SENSOR: ") + line);
    echoDiagTX(String("UNPARSED SENSOR: ") + line);
  }
}
