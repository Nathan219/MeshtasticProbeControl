#pragma once
// ==========================
// --- BASELINE CONSTANTS ---
// ==========================
// Baseline ranges per metric; used when areas are in baseline mode.
// Also serve as initial thresholds examples.
#define BASELINE_CO2_MIN      400.0f
#define BASELINE_CO2_MAX     1000.0f
#define BASELINE_TEMP_MIN      18.0f
#define BASELINE_TEMP_MAX      25.0f
#define BASELINE_HUM_MIN       30.0f
#define BASELINE_HUM_MAX       70.0f
#define BASELINE_DB_MIN        35.0f
#define BASELINE_DB_MAX        85.0f

// If live max reaches this fraction of baseline max (while in baseline mode),
// STATS will start reporting live min/max for that metric (but baseline:true remains).
#define BASELINE_NEAR_FRACTION 0.90f
