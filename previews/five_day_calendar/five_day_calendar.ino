// Deterministic EXAMPLE DATA only; no production acquisition or credentials.
#include <Seeed_GFX.h>
#include "../../application/psram_workspace.h"
#include "../../application/five_day_posix_time.h"

#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(ARDUINO_XIAO_ESP32S3_PLUS)
#error "Select XIAO ESP32-S3 Plus with OPI PSRAM"
#endif

static FiveDaySnapshot snapshot;
static Ee02PsramWorkspace workspace;
// Preview configuration only. Production timezone/configuration remains separate.
static FiveDayPosixTime::Context previewZone = {"CET-1CEST,M3.5.0,M10.5.0/3"};
static FiveDayRenderer::Report rendering;
static bool renderingAttempted = false;

static bool addTimed(int64_t start, int64_t end, const char* title) {
  FiveDaySnapshot::Input event = {};
  event.kind = FiveDaySnapshot::Kind::Timed; event.range.timed = {start, end};
  event.title = {title, strlen(title)};
  return snapshot.addOccurrence(0, event) == FiveDaySnapshot::Result::Ok;
}
static bool addAllDay(int32_t start, int32_t end, const char* title) {
  FiveDaySnapshot::Input event = {};
  event.kind = FiveDaySnapshot::Kind::AllDay; event.range.civil = {start, end};
  event.title = {title, strlen(title)};
  return snapshot.addOccurrence(0, event) == FiveDaySnapshot::Result::Ok;
}
static GfxResult poisonScratch(uint8_t* bytes, size_t size, void*) {
  memset(bytes, 0xa5, size); return GfxResult::success();
}
static GfxResult drawCalendar(Seeed_GFX& display, void*) {
  display.setRotation(1); // Logical 1600x1200; existing dividers at x/y 1044.
  display.fillScreen(TFT_WHITE); // Preview alone may clear the full framebuffer.
  display.drawLine(1044, 10, 1044, 1180, TFT_BLACK);
  display.drawLine(10, 1044, 1034, 1044, TFT_BLACK);
  display.setFreeFont(nullptr);
  const char* notice = "EXAMPLE DATA - NOT A LIVE CALENDAR";
  for (size_t i = 0; notice[i]; ++i)
    display.drawChar(10 + int32_t(i) * 12, 1060, notice[i], TFT_RED, TFT_WHITE, 2);
  renderingAttempted = true;
  rendering = FiveDayRenderer::render(display, snapshot, {10,10,1025,1025}, {FiveDayPosixTime::convert, &previewZone});
  return rendering.status == FiveDayRenderer::Status::Drawn ? GfxResult::success() :
      GfxResult(GfxError::InvalidArgument, "calendar renderer failed");
}
void setup() {
  Serial.begin(115200);
  Serial.println("EE02 five-day preview: deterministic EXAMPLE DATA; not live");
  if (setenv("TZ", previewZone.configuredTimezone, 1) != 0) { Serial.println("Preview TZ setup failed"); return; }
  tzset();
  // Oct 23-27 2026, Europe-style POSIX rules; Oct 25 has 25 hours.
  const FiveDaySnapshot::Window window = {{1792706400,1792792800,1792879200,1792969200,1793055600,1793142000},20749,0};
  const FiveDaySnapshot::Text labels[] = {{"Example personal",16},{"Example team",12}};
  if (snapshot.initialize(window, 32, 4096, labels, 2) != FiveDaySnapshot::Result::Ok ||
      !workspace.allocate(2097152).ok()) { Serial.println("Preview PSRAM allocation failed"); return; }
  if (snapshot.beginSource(0) != FiveDaySnapshot::Result::Ok) return;
  // Deliberately unordered. Identical repeated local times remain separate UTC instants.
  bool populated = addTimed(1792891800,1792892700,"Second 02:30 - standard time") &&
      addTimed(1792888200,1792889100,"First 02:30 - summer time") &&
      addTimed(1792789200,1792796400,"Cross-midnight handover") &&
      addTimed(1792746000,1792749600,"Example appointment") &&
      addTimed(1793026800,1793026800,"Point reminder") &&
      addTimed(1793116800,1793120400,"Last-day review") &&
      addAllDay(20749,20750,"All day: example holiday") &&
      addAllDay(20750,20753,"Multiday example visit");
  if (!populated || snapshot.completeSource(0) != FiveDaySnapshot::Result::Ok) {
    Serial.println("Preview population failed"); return;
  }
  if (snapshot.beginSource(1) != FiveDaySnapshot::Result::Ok ||
      snapshot.failSource(1, FiveDaySnapshot::Reason::ProducerFailure) != FiveDaySnapshot::Result::Ok) return;
  if (!workspace.withScratch(0, workspace.capacity(), poisonScratch).ok()) return;
  const auto result = workspace.render(drawCalendar);
  Serial.printf("Renderer called=%u status=%u entries=%u overflow=%u\n", unsigned(renderingAttempted), unsigned(rendering.status),
      unsigned(rendering.dayEntriesDrawn), unsigned(rendering.dayEntriesOmitted));
  Serial.printf("Render/refresh=%u; shutdown checked=%u result=%u\n", unsigned(result.operation.error),
      unsigned(result.shutdownChecked), unsigned(result.shutdown.error));
  // Retained storage must remain alive after uncertain panel communication/end.
  if (workspace.state() == Ee02PsramWorkspace::State::Scratch) {
    const auto released = workspace.release();
    Serial.printf("Workspace release=%u\n", unsigned(released.error));
  } else Serial.println("Workspace retained: no reuse/free; inspect panel shutdown");
  Serial.printf("Snapshot release=%u\n", unsigned(snapshot.release()));
}
void loop() {}
