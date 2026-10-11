// Isolated compile/link fixture; never included by the production sketch.
// Compilation does not validate panel operation. No flashing is part of this check.
#include <Seeed_GFX.h>

// Match the XIAO Plus catalog gate and the ESP32-S3 path in the pinned source.
// These checks do not establish an installed FQBN or verified board menu options.
#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32S3) || \
    !defined(ARDUINO_XIAO_ESP32S3_PLUS)
#error "Select the actual XIAO ESP32-S3 Plus board profile for this fixture"
#endif

Seeed_GFX display(Seeed_Product::Seeed_ePaper_13INCH3_C);

void setup() {
  if (!display.begin()) {
    return;
  }

  display.setRotation(1);
  display.fillScreen(TFT_WHITE);
  display.drawRect(10, 10, 200, 60, TFT_BLACK);
  display.drawLine(10, 80, 210, 80, TFT_RED);
  display.fillCircle(20, 100, 5, TFT_YELLOW);
  display.drawPixel(40, 100, TFT_GREEN);
  display.drawPixel(60, 100, TFT_BLUE);
  display.setTextColor(TFT_BLACK);
  display.drawString("EE02 GFX2 baseline", 20, 20);

  (void)display.refresh();
  (void)display.end();
}

void loop() {}
