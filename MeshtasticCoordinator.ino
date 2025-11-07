// ================================================================
//  MeshtasticCoordinator (ESP32-S3)
//  ================================
//  Coordinates two Meshtastic devices over UART.
//  - Diagnostics UART (pins RX=5, TX=6): commands & responses
//  - Sensor UART      (pins RX=2, TX=3): sensor data & probe messages
//
//  COMMAND SYNTAX OVERVIEW
//  ------------------------
//  GET AREAS
//    → AREA: {AREA} {LOCATION} {PROBE_ID}
//
//  GET STATS:
//    → STAT: {AREA} {METRIC} min:{V} max:{V} min_o:{V} max_o:{V} baseline:{true/false}
//
//  GET THRESHOLD:
//    → THRESHOLD {AREA} {METRIC} {V1} {V2} {V3} {V4} {V5} {V6}
//
//  GET USE_BASELINE {AREA}
//    → USE_BASELINE {AREA} True/False
//
//  SET USE_BASELINE {AREA} True/False
//    → USE_BASELINE {AREA} True ACCEPTED
//
//  SET OVERRIDE {AREA} MIN|MAX {VALUE}
//    → OVERRIDE {AREA} MIN|MAX {VALUE} ACCEPTED
//
//  SET THRESHOLD {AREA} {METRIC} {PIXEL#} {VALUE}
//    → THRESHOLD {AREA} {METRIC} {PIXEL#} {VALUE} ACCEPTED
//
//  SET PROBES {PROBE_ID} {AREA} {LOCATION}   (from Diagnostics or Sensor UART)
//    → PROBES {PROBE_ID} {AREA} {LOCATION} ACCEPTED
//
//  (Sensor-only) {PROBE_ID} SET PROBE {AREA} {LOCATION}
//    → PROBE {PROBE_ID} {AREA} {LOCATION} ACCEPTED
//
//  REMOVE PROBE {PROBE_ID}                   (from either UART)
//    → PROBE {PROBE_ID} REMOVED
//
//  Telemetry line on Sensor UART:
//    {PROBE_ID}: CO2:###,Temp:##,Hum:##,Sound:##
//
// ================================================================

#include <Arduino.h>
#include "Baselines.h"
#include "ConfigManager.h"
#include "SensorHandler.h"
#include "CommandParser.h"

// =======================
// --- UART CONFIG ---
// =======================
#define UART_BAUD        115200
#define DIAG_RX_PIN      5
#define DIAG_TX_PIN      6
#define SENSOR_RX_PIN    2
#define SENSOR_TX_PIN    3

HardwareSerial DiagSerial(2);  // UART2 for diagnostics
HardwareSerial SensorSerial(1); // UART1 for sensors

ConfigManager CONFIG;
SensorHandler SENSORS(&CONFIG);
CommandParser PARSER(&CONFIG, &SENSORS, &DiagSerial);

void setup() {
  Serial.begin(115200);
  delay(100);
  DiagSerial.begin(UART_BAUD, SERIAL_8N1, DIAG_RX_PIN, DIAG_TX_PIN);
  SensorSerial.begin(UART_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  Serial.println("[BOOT] MeshtasticCoordinator starting...");
  DiagSerial.println("READY");

  if (!CONFIG.begin()) {
    Serial.println("[ERR] Config init failed.");
    DiagSerial.println("ERR CONFIG");
  } else {
    Serial.println("[OK] Config init complete.");
  }

  SENSORS.setSensorSerial(&SensorSerial);
  SENSORS.setDiagSerial(&DiagSerial);
}
// ================================================================
//  Helper: timestamped debug prefix
// ================================================================
String nowStamp() {
  unsigned long ms = millis();
  unsigned long s = ms / 1000;
  unsigned long m = s / 60;
  unsigned long h = m / 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "[%02lu:%02lu.%03lu] ", m % 60, s % 60, ms % 1000);
  return String(buf);
}

void loop() {
  // Diagnostics UART lines
  if (DiagSerial.available()) {
    String line = DiagSerial.readStringUntil('\n');
    line.trim();
    if (line.length()) {
      Serial.print(nowStamp());
      Serial.print("[DIAG RX] ");
      Serial.println(line);
      PARSER.handleCommand(line);
    }
  }

  // Sensor UART lines
  if (SensorSerial.available()) {
    String line = SensorSerial.readStringUntil('\n');
    line.trim();
    if (line.length()) {
      Serial.print(nowStamp());
      Serial.print("[SENS RX] ");
      Serial.println(line);
      SENSORS.handleSensorLine(line);
    }
  }
    // Background scheduled tasks
  PARSER.processStatsJob();

}

