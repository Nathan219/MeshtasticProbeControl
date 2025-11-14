#include "LedMessenger.h"
#include "Metrics.h"
#include <vector>

extern void echoDiagTX(const String& msg);

// ================================================================
// ======= CONFIGURATION =======
#define STAR_HYSTERESIS_PCT 0.05   // 5% higher value gets a '*'

// Debug flags
// #define DEBUG_LED_LOOP
#define DEBUG_LED_THRESH
#define DEBUG_LED_SEND
#define DEBUG_LED_WARN

// ================================================================

LedMessenger::LedMessenger(ConfigManager* cfg,
                           SensorHandler* sensors,
                           Stream* usb)
  :     _cfg(cfg),
    _sensors(sensors),
    _usb(usb),
    _ledSerial(nullptr),
    _lastLedSend(0),
    _lastDiagSend(0),
    _lastEasterEggState(false),
    _debugCounter(0),
    _testModeEnabled(false),
    _testPixelValue(0),
    _lastTestIncrement(0)
{
  // Initialize ESPSoftwareSerial for LED communication
  // Using pin 8 as TX, pin 7 as RX (RX not actually used for one-way communication)
  _ledSerial = new SoftwareSerial(LED_SERIAL_RX_PIN, LED_SERIAL_TX_PIN);
  _ledSerial->begin(LED_SERIAL_BAUD);
  delay(100);  // Give serial time to initialize

  if (_usb) {
    _usb->println("[LED] Using ESPSoftwareSerial mode");
    _usb->printf("      RX Pin=%d, TX Pin=%d, Baud=%d\n", LED_SERIAL_RX_PIN, LED_SERIAL_TX_PIN, LED_SERIAL_BAUD);
  }
}

// ================================================================
void LedMessenger::loop() {
  unsigned long now = millis();
  if (!_cfg || !_sensors) return;

  // --- EASTER_EGG handling (still immediate on change) ---
  bool eeState = _cfg->global.easterEgg;
  if (eeState != _lastEasterEggState) {
    _lastEasterEggState = eeState;
    String eeMsg = String("EASTER_EGG ") + (eeState ? "ON" : "OFF");
    sendLed(eeMsg);
    if (_usb) _usb->println("[LED TX] " + eeMsg);
  }

  // --- Test mode: increment pixel value every 5 seconds ---
  if (_testModeEnabled) {
    if (now - _lastTestIncrement >= 5000) {  // 5 seconds
      _lastTestIncrement = now;
      _testPixelValue++;
      if (_testPixelValue > 6) {
        _testPixelValue = 0;  // Cycle back to 0
      }
    }
  }

  // --- LED board update timer ---
  if (now - _lastLedSend > _cfg->global.ledUpdateInterval) {
    _lastLedSend = now;
    _debugCounter++;  // Increment counter for throttling debug output

    auto& areas = _cfg->areas();
    int aggN = _cfg->global.aggregateN;

    // Helper to check if a metric is measured (has at least one non-(-1) threshold)
    // Uses fresh config lookup to ensure we have latest thresholds
    auto isMetricMeasured = [this](const String& areaName, Metric m) -> bool {
      AreaConfig* area = _cfg ? _cfg->findAreaByName(areaName) : nullptr;
      if (!area) return false;
      for (int i = 0; i < 6; i++) {
        if (area->thresholds[m].values[i] >= 0) return true;
      }
      return false;
    };

    // First pass: calculate pixels and collect data
    struct AreaData {
      AreaConfig* area;
      int pixel;
      float metricValues[MET_COUNT];
    };
    std::vector<AreaData> areaDataList;
    std::vector<AreaData*> maxPixelAreas;  // Areas with pixel == 6

    for (auto& a : areas) {
      int pixel;
      
      if (_testModeEnabled) {
        // Test mode: use the test pixel value for all areas
        pixel = _testPixelValue;
      } else {
        // Normal mode: calculate pixel for CO2 (primary metric for LED display)
        float co2Val = _sensors->areaMetricRecentAvg(a.name, MET_CO2, aggN);
        if (isnan(co2Val)) continue;  // Skip areas with no sensor data in normal mode
        pixel = calculatePixelForMetric(a, MET_CO2, co2Val);
      }
      
      a.rt.lastPixel = pixel;   // store for later diagnostics

      AreaData data;
      data.area = &a;
      data.pixel = pixel;
      
      // Collect values for all measured metrics (only in normal mode)
      if (!_testModeEnabled) {
        for (int mi = 0; mi < MET_COUNT; mi++) {
          Metric m = static_cast<Metric>(mi);
          if (isMetricMeasured(a.name, m)) {
            data.metricValues[mi] = _sensors->areaMetricRecentAvg(a.name, m, aggN);
          } else {
            data.metricValues[mi] = NAN;
          }
        }
      } else {
        // In test mode, set dummy values
        for (int mi = 0; mi < MET_COUNT; mi++) {
          data.metricValues[mi] = NAN;
        }
      }

      areaDataList.push_back(data);
      if (pixel == 6) {
        maxPixelAreas.push_back(&areaDataList.back());
      }
    }

    // Determine which area gets the "*" if >= 2 areas have max pixels
    AreaData* highlightArea = nullptr;
    
#ifdef DEBUG_LED_THRESH
    // Only log star calculation every 10th update
    bool shouldLogStar = (_debugCounter % 10 == 0);
    if (_usb && maxPixelAreas.size() > 0 && shouldLogStar) {
      _usb->printf("\n[LED STAR] Found %d area(s) with max pixels (6)\n", maxPixelAreas.size());
    }
#endif
    
      if (maxPixelAreas.size() >= 2) {
      float bestScore = -INFINITY;
      
#ifdef DEBUG_LED_THRESH
      if (_usb && shouldLogStar) {
        _usb->println("[LED STAR] Calculating scores for star assignment...");
      }
#endif
      
      for (auto* areaData : maxPixelAreas) {
        // Calculate score: sum of normalized values for all measured metrics
        float score = 0.0f;
        int measuredCount = 0;
        
        for (int mi = 0; mi < MET_COUNT; mi++) {
          if (!isnan(areaData->metricValues[mi])) {
            Metric m = static_cast<Metric>(mi);
            // Normalize by baseline max to make metrics comparable
            float normalized = areaData->metricValues[mi] / METRIC_BASELINE_MAX[m];
            score += normalized;
            measuredCount++;
          }
        }
        
        // Average score across measured metrics
        if (measuredCount > 0) {
          score = score / measuredCount;
        }
        
#ifdef DEBUG_LED_THRESH
        if (_usb && shouldLogStar) {
          _usb->print("[LED STAR]   ");
          _usb->print(areaData->area->name);
          _usb->printf(": score=%.4f (from %d measured metrics)\n", score, measuredCount);
          for (int mi = 0; mi < MET_COUNT; mi++) {
            if (!isnan(areaData->metricValues[mi])) {
              Metric m = static_cast<Metric>(mi);
              float normalized = areaData->metricValues[mi] / METRIC_BASELINE_MAX[m];
              _usb->print("[LED STAR]     ");
              _usb->print(metricToString(m));
              _usb->printf(": %.2f / %.1f = %.4f\n",
                           areaData->metricValues[mi],
                           METRIC_BASELINE_MAX[m],
                           normalized);
            }
          }
        }
#endif
        
        if (score > bestScore) {
          bestScore = score;
          highlightArea = areaData;
        }
      }
      
#ifdef DEBUG_LED_THRESH
      if (_usb && highlightArea && shouldLogStar) {
        _usb->print("[LED STAR] => Winner: ");
        _usb->print(highlightArea->area->name);
        _usb->printf(" (score=%.4f)\n\n", bestScore);
      }
#endif
    }

    // Second pass: send LED messages
    for (auto& data : areaDataList) {
      String msg = data.area->name + " " + String(data.pixel);
      if (&data == highlightArea) {
        msg += "*";
      }
      sendLed(msg);
      delay(10);  // Small delay between I2C transmissions to allow device to process
    }
  }

  // --- Diagnostic broadcast timer ---
  if (now - _lastDiagSend > _cfg->global.diagPixelInterval) {
    _lastDiagSend = now;

    String msg = "[LEDS] Pixels: ";
    bool first = true;
    for (auto& a : _cfg->areas()) {
      if (!first) msg += ", ";
      msg += a.name + ":" + String(a.rt.lastPixel);
      first = false;
    }
    if (_usb) _usb->println(msg);
    echoDiagTX(msg);
  }
}

// ================================================================
int LedMessenger::calculatePixelForMetric(const AreaConfig& a, Metric m, float currentVal) {
  // Look up the area directly from config to ensure we always have the latest thresholds
  AreaConfig* areaConfig = _cfg ? _cfg->findAreaByName(a.name) : nullptr;
  if (!areaConfig) {
    // Fallback to passed-in area if lookup fails
    areaConfig = const_cast<AreaConfig*>(&a);
  }
  
  int pixel = 0;
  float liveMax = a.rt.liveMax[m];  // Use runtime data from passed area
  if (liveMax <= 0) liveMax = 1;

#ifdef DEBUG_LED_THRESH
  // Only log detailed threshold info every 10th update to reduce spam
  bool shouldLog = (_debugCounter % 10 == 0);
  if (_usb && shouldLog) {
    _usb->println("\n[LED THRESH] ========================================");
    _usb->println("[LED THRESH] area=" + a.name + " metric=" + metricToString(m));
    _usb->printf("[LED THRESH] current=%.2f liveMax=%.1f overrideMax=%.1f\n", 
                 currentVal, liveMax, a.overrideMax > 0 ? a.overrideMax : -1.0f);
    _usb->print("[LED THRESH] Raw thresholds: [");
    for (int i = 0; i < 6; i++) {
      float th = areaConfig->thresholds[m].values[i];
      if (i > 0) _usb->print(", ");
      if (th < 0) {
        _usb->print("unused");
      } else {
        _usb->printf("%.3f", th);
      }
    }
    _usb->println("]");
  }
#endif

  for (int i = 0; i < 6; i++) {
    // Use thresholds from the fresh config lookup
    float th = areaConfig->thresholds[m].values[i];
    if (th < 0) continue;
    float effective = th;

    // If threshold < 1, treat as percentage of current range
    if (th > 0 && th < 1.0f) {
        float rangeMax = a.overrideMax > 0 ? a.overrideMax : a.rt.liveMax[m];
        if (rangeMax <= 0) rangeMax = METRIC_BASELINE_MAX[m]; // fallback
        effective = th * rangeMax;
    }

    #ifdef DEBUG_LED_THRESH
    if (_usb && shouldLog) {
        float rangeMax = a.overrideMax > 0 ? a.overrideMax : a.rt.liveMax[m];
        if (rangeMax <= 0) rangeMax = METRIC_BASELINE_MAX[m];
        _usb->printf(
          "[LED THRESH]   idx=%d: raw=%.3f -> eff=%.1f (rangeMax=%.1f, baseline=%.1f) | current=%.2f %s\n",
          i, th, effective, rangeMax, METRIC_BASELINE_MAX[m], currentVal,
          (currentVal >= effective) ? "[PASS]" : "[FAIL]");
    }
#endif
    if (currentVal >= effective) pixel = i + 1;
  }

#ifdef DEBUG_LED_THRESH
  if (_usb && shouldLog) {
    _usb->printf("[LED THRESH] => RESULT: pixel=%d\n", pixel);
    _usb->println("[LED THRESH] ========================================\n");
  }
#endif

  return pixel;
}

void LedMessenger::sendLed(const String& msg) {
  if (!_ledSerial) return;
  
  // Send message over ESPSoftwareSerial
  _ledSerial->println(msg);

#ifdef DEBUG_LED_SEND
  if (_usb) {
    _usb->println("[LED TX] " + msg);
  }
#endif
}

String LedMessenger::sendLedWithResponse(const String& msg) {
  if (!_ledSerial) return String("");
  
  // Clear any existing data in the buffer
  while (_ledSerial->available()) {
    _ledSerial->read();
  }
  
  // Send message over ESPSoftwareSerial
  _ledSerial->println(msg);

#ifdef DEBUG_LED_SEND
  if (_usb) {
    _usb->println("[LED TX] " + msg);
  }
#endif

  // Wait a bit for the response
  delay(100);
  
  // Read response
  String response = "";
  unsigned long startTime = millis();
  const unsigned long timeout = 1000;  // 1 second timeout
  
  while (millis() - startTime < timeout) {
    if (_ledSerial->available()) {
      char c = _ledSerial->read();
      if (c == '\n' || c == '\r') {
        if (response.length() > 0) break;  // Got a complete line
      } else {
        response += c;
      }
    }
    delay(10);
  }
  
  response.trim();
  
#ifdef DEBUG_LED_SEND
  if (_usb) {
    _usb->println("[LED RX] " + response);
  }
#endif
  
  return response;
}

void LedMessenger::setTestMode(bool enabled) {
  _testModeEnabled = enabled;
  if (enabled) {
    _testPixelValue = 0;  // Reset to 0 when enabling
    _lastTestIncrement = millis();  // Start timer
    if (_usb) {
      _usb->println("[LED] Test mode ENABLED - cycling pixels 0-6 every 5 seconds");
    }
  } else {
    if (_usb) {
      _usb->println("[LED] Test mode DISABLED - returning to normal operation");
    }
  }
}

