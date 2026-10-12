// Isolated compile/link fixture; never included by the production sketch.
// Compilation does not validate panel operation. No flashing is part of this check.
#include <Seeed_GFX.h>
#include <runtime/ProductCatalog.h>
#include <runtime/DisplayInstance.h>
#include <panel/Panel_EPaper.h>
#include <esp32-hal-psram.h>

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

static void borrowedBufferCase() {
  constexpr size_t framebufferBytes = 960000;
  if (!psramFound()) return;
  uint8_t* storage = static_cast<uint8_t*>(ps_malloc(framebufferBytes));
  if (!storage) return; // No large internal-RAM fallback.

  // Official factory owns Board/Bus/Driver/Panel, before panel initialization.
  // The instance outlives the externally attached facade and caller storage.
  DisplayInstance instance;
  const GfxResult created = ProductCatalog::create(
      Seeed_Product::Seeed_ePaper_13INCH3_C, instance);
  if (!created.ok()) { free(storage); return; }
  auto* panel = static_cast<Panel_EPaper*>(instance.panel());
  if (!panel->setBorrowedFrameBuffer(storage, framebufferBytes).ok()) {
    instance.reset(); free(storage); return;
  }
  GfxResult ended;
  {
    Seeed_GFX display(*panel);
    if (display.begin() && panel->configure(PanelMode::Colorful).ok()) {
      display.setRotation(1);
      display.fillScreen(TFT_WHITE);
      display.drawPixel(0, 0, TFT_BLACK);
      display.drawRect(10, 10, 200, 60, TFT_RED);
      display.drawLine(10, 80, 210, 80, TFT_GREEN);
      display.fillCircle(20, 100, 5, TFT_BLUE);
      display.setTextColor(TFT_YELLOW);
      display.drawString("EE02 borrowed framebuffer", 20, 20);
      (void)display.refresh();
    }
    // Check the concrete panel result: facade/instance repeated end() can
    // otherwise mask a previous failure. Destruction is not quiescence proof.
    ended = panel->end();
  }
  instance.reset(); // Delete the panel before releasing application storage.
  if (ended.ok()) free(storage);
  // On uncertain hardware shutdown retain storage; no automatic recovery.
}

void setup() {
  if (ownedBufferCase()) borrowedBufferCase();
}
void loop() {}
