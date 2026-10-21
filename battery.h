#pragma once

#include "TFT_eSPI.h"
#include "constants.h"
#include "application/battery_measurement.h"

// ============================================================
// Draw battery charge indicator in the lower right corner
// ============================================================
void drawBattery(EPaper &epaperRef) {
  int pct = voltageToPercent(readBatteryMillivolts());

  // Battery icon geometry defined in constants.h
  int termW = 4;  // terminal bump width
  int termH = 12; // terminal bump height
  int radius = 5; // corner radius for rounded rect

  // Terminal bump (right side)
  int termX = BAT_ICON_X + BAT_ICON_W;
  int termY = BAT_ICON_Y + (BAT_ICON_H - termH) / 2;
  epaperRef.fillRect(termX, termY, termW, termH, TFT_BLACK);

  // Battery body outline (rounded rectangle)
  epaperRef.drawRoundRect(BAT_ICON_X, BAT_ICON_Y, BAT_ICON_W, BAT_ICON_H,
                          radius, TFT_BLACK);

  // Fill level inside the body
  int margin = 3;
  int fillW = BAT_ICON_W - 2 * margin;
  int fillH = BAT_ICON_H - 2 * margin;
  int fillActualW = (fillW * pct) / 100;
  int fillX = BAT_ICON_X + margin;
  int fillY = BAT_ICON_Y + margin;

  if (fillActualW > 0) {
    epaperRef.fillRect(fillX, fillY, fillActualW, fillH, TFT_BLACK);
  }

  // Percentage label (centered over the battery body)
  epaperRef.setFreeFont(&FreeSans9pt7b);
  epaperRef.setTextColor(TFT_BLACK);
  char pctText[6];
  snprintf(pctText, sizeof(pctText), "%d%%", pct);
  int tw = epaperRef.textWidth(pctText);
  int th = epaperRef.fontHeight();

  // Position text to the left of the battery icon
  int textX = BAT_ICON_X - tw - BAT_TEXT_GAP;
  int textY = BAT_ICON_Y + (BAT_ICON_H - th) / 2 + 3;

  // Clear the text background area first
  epaperRef.fillRect(textX - 2, BAT_ICON_Y, tw + 4, BAT_ICON_H, TFT_WHITE);
  epaperRef.drawString(pctText, textX, textY);

  Serial.printf("Battery: %dmV — %d%%\n", readBatteryMillivolts(), pct);
}
