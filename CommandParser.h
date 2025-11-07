#pragma once
#include <Arduino.h>
#include "ConfigManager.h"
#include "SensorHandler.h"
#include "Metrics.h"


struct StatsJob {
  bool active = false;
  size_t areaIndex = 0;
  int metricIndex = 0;
  unsigned long lastSend = 0;
};

// ================================================================
//  CommandParser
// ================================================================
//
// Handles text commands coming from diagnostics UART or USB serial.
// Examples:
//   GET STATS
//   GET STATS FLOOR11
//   GET HISTORY DFE8
//   SET OVERRIDE FLOOR11 MAX 800
//   SET THRESHOLD FLOOR11 CO2 3 500
// ================================================================
class CommandParser {
public:
  CommandParser(ConfigManager* cfg, SensorHandler* sens, HardwareSerial* diag, Stream* usb)
    : _cfg(cfg), _sensors(sens), _diag(diag), _usb(usb) {}

  void handleCommand(const String& line) ;
  void processStatsJob();

private:
  ConfigManager*   _cfg;
  SensorHandler*   _sensors;
  HardwareSerial*  _diag;
  Stream*          _usb;
  StatsJob         _statsJob;
  bool _sendingStats = false;
  int  _sendIndex = 0;

  unsigned long _lastStatsSent = 0;
  unsigned long _statsIntervalMs = 10000;  // 10 s default interval

  void sendDiag(const String& msg);
  void sendUSB(const String& msg);
  void printStatLine(const AreaConfig& a, Metric m);
};
