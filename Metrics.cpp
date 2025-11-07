#include "Metrics.h"

String metricToString(Metric m) {
  switch (m) {
    case MET_CO2:  return "CO2";
    case MET_TEMP: return "TEMP";
    case MET_HUM:  return "HUM";
    case MET_DB:   return "DB";
    default:       return "INVALID";
  }
}

Metric metricFromString(const String& s) {
  String u = s; 
  u.toUpperCase();

  // Handle common variations and typos
  if (u == "CO2" || u == "C02" || u == "CARBONDIOXIDE")
    return MET_CO2;
  if (u == "TEMP" || u == "TEMPERATURE")
    return MET_TEMP;
  if (u == "HUM" || u == "HUMIDITY" || u == "RH")
    return MET_HUM;
  if (u == "DB" || u == "SOUND" || u == "DECIBEL")
    return MET_DB;

  return MET_INVALID;
}