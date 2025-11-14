#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "ConfigManager.h"
#include "SensorHandler.h"
#include "Metrics.h"

// ================================================================
// ======= CONFIGURATION =======
#define LED_SERIAL_TX_PIN 8      // TX pin for ESPSoftwareSerial
#define LED_SERIAL_RX_PIN 7      // RX pin (dummy, not used for one-way communication)
#define LED_SERIAL_BAUD 38400      // Baud rate for LED communication
#define LED_UPDATE_INTERVAL 1000
#define STAR_HYSTERESIS_PCT 0.05

class LedMessenger {
 public:
  LedMessenger(ConfigManager* cfg,
               SensorHandler* sensors,
               Stream* usb);

  void loop();
  void sendLed(const String& msg);
  String sendLedWithResponse(const String& msg);  // Send message and read response
  int  calculatePixelForMetric(const AreaConfig& a, Metric m, float currentVal);
  void setTestMode(bool enabled);  // Enable/disable test mode

 private:
  ConfigManager* _cfg;
  SensorHandler* _sensors;
  Stream* _usb;
  SoftwareSerial* _ledSerial;

  unsigned long _lastDiagSend;
  unsigned long _lastLedSend;
  bool  _lastEasterEggState;
  int   _debugCounter;  // Counter to throttle debug output
  
  // Test mode state
  bool  _testModeEnabled;
  int   _testPixelValue;
  unsigned long _lastTestIncrement;
};
