#pragma once
#include <Arduino.h>
#include "ConfigManager.h"
#include "SensorHandler.h"

#define STATS_MESSAGE_INTERVAL_MS 10000  // 10 seconds between STAT lines

class CommandParser {
public:
  CommandParser(ConfigManager* cfg, SensorHandler* sens, HardwareSerial* out)
    : _cfg(cfg), _sens(sens), _out(out) {}

  void handleCommand(const String& line);
  void processStatsJob();  // call periodically from loop()

private:
  ConfigManager*  _cfg;
  SensorHandler*  _sens;
  HardwareSerial* _out;

  struct StatsJob {
    bool active = false;
    unsigned long lastSent = 0;
    size_t areaIndex = 0;
    size_t metricIndex = 0;
  } _statsJob;

  // command handlers
  void cmdGetAreas();
  void cmdGetStats();
  void cmdGetThreshold();
  void cmdGetUseBaseline(const String& area);
  void cmdSetUseBaseline(const String& area, bool on);
  void cmdSetOverride(const String& area, bool isMin, float val);
  void cmdSetThreshold(const String& area, Metric m, int pix, float val);
  void cmdSetProbes(const String& probe, const String& area, const String& loc);
  void cmdRemoveProbe(const String& probe);
  void cmdGetStatsInterval();                        // ✅ added
  void cmdSetStatsInterval(unsigned long val);       // ✅ added

  void printStatLine(const AreaConfig& a, Metric m);

};
