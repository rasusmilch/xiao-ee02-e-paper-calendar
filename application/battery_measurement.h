#pragma once

#include <Arduino.h>

// Existing production measurement/calibration only; no graphics or observation store.
// ============================================================
// Battery voltage measurement pins
// ============================================================
#define VOLTAGE_PIN A0    // GPIO1  — ADC input from voltage divider
#define ADC_ENABLE_PIN A5 // GPIO6  — pull HIGH to enable measurement

// ============================================================
// LiPo discharge curve: voltage (mV) → charge percentage
// Typical 3.7V LiPo; linear interpolation is used between points.
// ============================================================
struct VoltagePoint {
  int mv;
  int pct;
};

static const VoltagePoint DISCHARGE_CURVE[] = {
    {4200, 100}, {4030, 90}, {3950, 80}, {3870, 70}, {3800, 60}, {3740, 50},
    {3680, 40},  {3610, 30}, {3520, 20}, {3430, 10}, {3300, 0},
};
static const int CURVE_POINTS =
    sizeof(DISCHARGE_CURVE) / sizeof(DISCHARGE_CURVE[0]);

// Map a measured millivolt value to a charge percentage (0–100)
int voltageToPercent(int mv) {
  if (mv >= DISCHARGE_CURVE[0].mv)
    return 100;
  if (mv <= DISCHARGE_CURVE[CURVE_POINTS - 1].mv)
    return 0;

  for (int i = 0; i < CURVE_POINTS - 1; i++) {
    int vHi = DISCHARGE_CURVE[i].mv;
    int vLo = DISCHARGE_CURVE[i + 1].mv;
    if (mv <= vHi && mv >= vLo) {
      int pctHi = DISCHARGE_CURVE[i].pct;
      int pctLo = DISCHARGE_CURVE[i + 1].pct;
      float t = (float)(vHi - mv) / (float)(vHi - vLo);
      return pctHi - (int)(t * (pctHi - pctLo) + 0.5f);
    }
  }
  return 0;
}

// Read battery voltage in millivolts.
// Returns 0 if the measurement cannot be taken.
int readBatteryMillivolts() {
  pinMode(ADC_ENABLE_PIN, OUTPUT);
  digitalWrite(ADC_ENABLE_PIN, HIGH); // enable voltage divider
  delay(5);                           // let the signal settle

  int raw = analogRead(VOLTAGE_PIN);

  digitalWrite(ADC_ENABLE_PIN, LOW); // disable to save power

  // XIAO ESP32S3 Plus: 2:1 divider (R1=R2=2MΩ) → VBAT = V_ADC * 2
  // ADC reference = 3.3 V, 12-bit ⇒ 0–4095 → 0–3300 mV
  float vAdc = (float)raw * 3300.0f / 4095.0f;
  int vBat = (int)(vAdc * 2.0f + 0.5f);
  return vBat;
}
