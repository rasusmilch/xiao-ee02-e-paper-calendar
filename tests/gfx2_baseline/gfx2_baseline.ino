// Isolated compile/link fixture; never included by the production sketch.
// Compilation does not validate panel operation. No flashing is part of this check.
#include <Seeed_GFX.h>
#include "../../application/psram_workspace.h"
#include "../../application/five_day_snapshot.h"
#include "../../application/ics_structure.h"

static bool structureEvent(const IcsStructure::Event& event, void* context) {
  if (event.type == IcsStructure::EventType::Property) {
    ++*static_cast<size_t*>(context); // No view escapes and no snapshot calls.
    if (IcsStructure::identifier(event.property.name, "UID"))
      return event.component.kind == IcsStructure::Kind::Participant &&
             event.component.parentOrdinal == 2;
  }
  return true;
}

static GfxResult structureScratch(uint8_t* storage, size_t bytes, void*) {
  const IcsStructure::Limits limits = {512,8,8,16,4,32,64,128,32,2};
  size_t required = 0, properties = 0;
  if (IcsStructure::scratchBytes(limits, required) != IcsStructure::Status::InProgress || bytes < required)
    return GfxResult(GfxError::InvalidArgument, "structural scratch too small");
  IcsStructure parser;
  if (parser.initialize(storage, bytes, limits, structureEvent, &properties) != IcsStructure::Status::InProgress)
    return GfxResult(GfxError::InvalidArgument, "structural initialization failed");
  const char* const lines[] = {"BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:synthetic",
    "BEGIN:VEVENT", "BEGIN:PARTICIPANT", "UID:person", "END:PARTICIPANT", "END:VEVENT", "END:VCALENDAR"};
  for (const char* line : lines)
    if (parser.consume(reinterpret_cast<const uint8_t*>(line), strlen(line)) != IcsStructure::Status::InProgress)
      return GfxResult(GfxError::InvalidArgument, "structural input failed");
  return parser.finish() == IcsStructure::Status::StructurallyComplete && properties == 3
      ? GfxResult::success() : GfxResult(GfxError::InvalidArgument, "structural finalization failed");
}

static_assert(alignof(FiveDaySnapshot::Occurrence) == 8 &&
              alignof(FiveDaySnapshot::Source) == 4 &&
              alignof(FiveDaySnapshot::Window) == 8, "review target snapshot layout");

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

static bool checkSnapshot(const FiveDaySnapshot& snapshot) {
  const auto title = snapshot.title(0);
  return snapshot.occurrenceCount() == 1 && title.size == 20 &&
         memcmp(title.data, "Synthetic occurrence", 21) == 0 &&
         snapshot.source(0)->outcome == FiveDaySnapshot::Outcome::Complete;
}

static GfxResult stageSynthetic(uint8_t* storage, size_t bytes, void* context) {
  const char title[] = "Synthetic occurrence";
  if (bytes < sizeof(title)) return GfxResult(GfxError::InvalidArgument, "scratch too small");
  memcpy(storage, title, sizeof(title));
  FiveDaySnapshot::Input input = {};
  input.kind = FiveDaySnapshot::Kind::Timed;
  input.range.timed = {3600, 3600};
  input.title = {reinterpret_cast<const char*>(storage), sizeof(title) - 1};
  auto& snapshot = *static_cast<FiveDaySnapshot*>(context);
  return snapshot.addOccurrence(0, input) == FiveDaySnapshot::Result::Ok
      ? GfxResult::success() : GfxResult(GfxError::InvalidArgument, "synthetic staging failed");
}

static GfxResult drawBorrowed(Seeed_GFX& display, void* context) {
  if (!checkSnapshot(*static_cast<FiveDaySnapshot*>(context)))
    return GfxResult(GfxError::InvalidArgument, "snapshot changed after scratch reuse");
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
  FiveDaySnapshot snapshot;
  // Synthetic UTC dates only; no timezone resolution or real feeds. All sizing
  // arguments are evaluation parameters, not product limits or defaults.
  const FiveDaySnapshot::Window window = {{0,86400,172800,259200,345600,432000},0,0};
  FiveDaySnapshot::Text labels[16];
  for (auto& label : labels) label = {"Synthetic calendar",18};
  if (snapshot.initialize(window, 1024, 262144, labels, 16) != FiveDaySnapshot::Result::Ok) return;
  Ee02PsramWorkspace workspace;
  if (!workspace.allocate(evaluationBytes).ok()) return;
  if (!workspace.withScratch(0, 1024, structureScratch).ok()) return;
  if (snapshot.beginSource(0) != FiveDaySnapshot::Result::Ok) return;
  if (!workspace.withScratch(0, 64, stageSynthetic, &snapshot).ok()) return;
  if (snapshot.completeSource(0) != FiveDaySnapshot::Result::Ok) return;
  for (size_t i = 1; i < 16; ++i) {
    if (snapshot.beginSource(i) != FiveDaySnapshot::Result::Ok) return;
    const auto outcome = i == 1
        ? snapshot.failSource(i, FiveDaySnapshot::Reason::ProducerFailure)
        : snapshot.completeSource(i); // Valid zero-event calendars.
    if (outcome != FiveDaySnapshot::Result::Ok) return;
  }
  uint8_t prefixPattern = 0x5a, tailPattern = 0x3c;
  constexpr size_t prefixBytes = Ee02PsramWorkspace::FrameBufferBytes;
  if (!workspace.withScratch(0, prefixBytes, fillScratch, &prefixPattern).ok()) return;
  if (!workspace.withScratch(prefixBytes, evaluationBytes - prefixBytes,
                             fillScratch, &tailPattern).ok()) return;
  if (!checkSnapshot(snapshot)) return;
  const auto result = workspace.render(drawBorrowed, &snapshot);
  if (!result.ok()) return; // Retained allocation is not freed by destruction.
  if (!workspace.withScratch(prefixBytes, evaluationBytes - prefixBytes,
                             checkTail, &tailPattern).ok()) return;
  if (!checkSnapshot(snapshot)) return;
  (void)workspace.release();
  (void)snapshot.release();
}

void setup() {
  if (ownedBufferCase()) borrowedBufferCase();
}
void loop() {}
