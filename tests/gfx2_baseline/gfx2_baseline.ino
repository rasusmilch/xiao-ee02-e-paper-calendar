// Isolated compile/link fixture; never included by the production sketch.
// Compilation does not validate panel operation. No flashing is part of this check.
#include <Seeed_GFX.h>
#include "../../application/psram_workspace.h"

// Match the XIAO Plus catalog gate and the ESP32-S3 path in the pinned source.
// These checks do not establish an installed FQBN or verified board menu options.
#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32S3) || \
    !defined(ARDUINO_XIAO_ESP32S3_PLUS)
#error "Select the actual XIAO ESP32-S3 Plus board profile for this fixture"
#endif

static bool ownedBufferCase() {
  Seeed_GFX display(Seeed_Product::Seeed_ePaper_13INCH3_C);
  if (!display.begin()) {
    return false;
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

  const GfxResult refreshed = display.refresh();
  const GfxResult ended = display.end();
  return refreshed.ok() && ended.ok();
}

static GfxResult fillScratch(uint8_t* storage, size_t bytes, void* context) {
  memset(storage, *static_cast<uint8_t*>(context), bytes);
  return GfxResult::success();
}

static GfxResult checkTail(uint8_t* storage, size_t bytes, void* context) {
  const uint8_t expected = *static_cast<uint8_t*>(context);
  for (size_t i = 0; i < bytes; ++i) {
    if (storage[i] != expected)
      return GfxResult(GfxError::InvalidArgument, "workspace tail changed");
  }
  return GfxResult::success();
}

static GfxResult drawBorrowed(Seeed_GFX& display, void*) {
  display.setRotation(1);
  display.fillScreen(TFT_WHITE);
  display.drawPixel(0, 0, TFT_BLACK);
  display.drawRect(10, 10, 200, 60, TFT_RED);
  display.drawLine(10, 80, 210, 80, TFT_GREEN);
  display.fillCircle(20, 100, 5, TFT_BLUE);
  display.setTextColor(TFT_YELLOW);
  display.drawString("EE02 PSRAM workspace", 20, 20);
  return GfxResult::success();
}

static void borrowedBufferCase() {
  constexpr size_t evaluationBytes = 2097152; // Provisional, not a product default.
  Ee02PsramWorkspace workspace;
  if (!workspace.allocate(evaluationBytes).ok()) return;
  uint8_t prefixPattern = 0x5a, tailPattern = 0x3c;
  constexpr size_t prefixBytes = Ee02PsramWorkspace::FrameBufferBytes;
  if (!workspace.withScratch(0, prefixBytes, fillScratch, &prefixPattern).ok()) return;
  if (!workspace.withScratch(prefixBytes, evaluationBytes - prefixBytes,
                             fillScratch, &tailPattern).ok()) return;
  const auto result = workspace.render(drawBorrowed);
  if (!result.ok()) return; // Retained allocation is not freed by destruction.
  if (!workspace.withScratch(prefixBytes, evaluationBytes - prefixBytes,
                             checkTail, &tailPattern).ok()) return;
  (void)workspace.release();
}

void setup() {
  if (ownedBufferCase()) borrowedBufferCase();
}
void loop() {}
