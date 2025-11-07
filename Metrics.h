#pragma once
#include <Arduino.h>

// ================================================================
//  Metric enum and helpers
// ================================================================
enum Metric {
  MET_CO2,
  MET_TEMP,
  MET_HUM,
  MET_DB,
  MET_COUNT,
  MET_INVALID 
};

String metricToString(Metric m);
Metric metricFromString(const String& s);
