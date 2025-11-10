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


static const float METRIC_BASELINE_MAX[MET_COUNT] = {
  2000.0f, // CO2 ppm
  30.0f,   // Temperature °C
  70.0f,  // Humidity %
  100.0f   // Sound dB
};

String metricToString(Metric m);
Metric metricFromString(const String& s);
