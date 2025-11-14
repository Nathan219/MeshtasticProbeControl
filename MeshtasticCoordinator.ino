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
#include <SPIFFS.h>
#include "ConfigManager.h"
#include "SensorHandler.h"
#include "CommandParser.h"
#include "LedMessenger.h"

#define FSYSTEM SPIFFS

// ================================================================
// --- UART CONFIGURATION ---
// ================================================================
#define SENSOR_RX_PIN 2
#define SENSOR_TX_PIN 3

#define DIAG_RX_PIN   5
#define DIAG_TX_PIN   6

#define BAUD_RATE     38400

// ================================================================
// --- TIMING CONFIGURATION ---
// ================================================================
#define LOOP_DELAY_MS 10  // Base delay for loop
#define STATS_JOB_INTERVAL_MS 10000  // Default interval for stats updates

// ================================================================
// --- OBJECTS ---
// ================================================================
HardwareSerial SensorSerial(1);
HardwareSerial DiagSerial(2);


ConfigManager CONFIG;
SensorHandler* SENSORS = nullptr;
CommandParser* PARSER = nullptr;
LedMessenger* LEDS = nullptr;


// ================================================================
// --- DIAGNOSTIC ECHO (shared utility) ---
// ================================================================
void echoDiagTX(const String& msg) {
  unsigned long now = millis();
  char buf[16];
  snprintf(buf, sizeof(buf), "[%02lu:%02lu.%03lu]",
           (now / 60000UL) % 60,
           (now / 1000UL) % 60,
           now % 1000);
  String full = String(buf) + " [DIAG TX] " + msg;

  Serial.println(full);  // USB console
  DiagSerial.println(full);  // send to diagnostics UART
}

// ================================================================
// --- SETUP ---
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[BOOT] Meshtastic Coordinator starting...");

  // --- Initialize filesystem ---
  if (!FSYSTEM.begin()) {
    Serial.println("[ERR] FSYSTEM mount failed, formatting...");
    FSYSTEM.format();
    FSYSTEM.begin();
  }

  Serial.println("Listing FSYSTEM files...");
  File root = FSYSTEM.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf(" - %s (%u bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }

  // --- Initialize UARTs ---
  SensorSerial.begin(BAUD_RATE, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  DiagSerial.begin(BAUD_RATE, SERIAL_8N1, DIAG_RX_PIN, DIAG_TX_PIN);
  Serial.println("[INIT] UARTs configured");
  Serial.println("[INIT] Loading configuration...");

  if (!CONFIG.loadFromFS()) {
    Serial.println("[WARN] Failed to load config. Using defaults.");
    CONFIG.ensureDefaults();
    CONFIG.save();
  }
  
  // --- Create handlers ---
  SENSORS = new SensorHandler(&CONFIG, &Serial);
  SENSORS->setSensorSerial(&SensorSerial);
  SENSORS->setDiagSerial(&DiagSerial);

  LEDS = new LedMessenger(&CONFIG, SENSORS, &Serial);

  PARSER = new CommandParser(&CONFIG, SENSORS, &DiagSerial, &Serial, LEDS);

  Serial.println("[INIT] Setup complete. Coordinator ready.");
  echoDiagTX("Coordinator boot complete.");
}

// ================================================================
// --- LOOP ---
// ================================================================
void loop() {
  // --------------------------------------------------------------
  // Handle diagnostics UART input (commands)
  // --------------------------------------------------------------
  static String diagLine;
  while (DiagSerial.available()) {
    char c = DiagSerial.read();
    if (c == '\r' || c == '\n') {
      diagLine.trim();
      if (diagLine.length() > 0) {
        Serial.println("[DIAG RX] " + diagLine);
        PARSER->handleCommand(diagLine);
      }
      diagLine = "";
    } else {
      diagLine += c;
    }
  }

  // --------------------------------------------------------------
  // Handle sensor UART input (sensor data + probe registration)
  // --------------------------------------------------------------
  static String sensLine;
  while (SensorSerial.available()) {
    char c = SensorSerial.read();
    if (c == '\r' || c == '\n') {
      sensLine.trim();
      if (sensLine.length() > 0) {
        Serial.println("[SENS RX] " + sensLine);
        SENSORS->handleSensorMessage(sensLine);
      }
      sensLine = "";
    } else {
      sensLine += c;
    }
  }

  // --------------------------------------------------------------
  // Periodic STATS job (uses internal timer)
  // --------------------------------------------------------------
  if (PARSER) {
    PARSER->processStatsJob();
  }
  if (LEDS) {
    LEDS->loop();
  } else {
      String msg = "LEDS NOT INITIALIZED!!";
      Serial.println(msg);
      DiagSerial.println(msg);
  }


  delay(LOOP_DELAY_MS);
}

