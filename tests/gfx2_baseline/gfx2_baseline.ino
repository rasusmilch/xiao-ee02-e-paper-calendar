// Isolated compile/link fixture; never included by the production sketch.
// Compilation does not validate panel operation. No flashing is part of this check.
#include <Seeed_GFX.h>
#include "../../application/psram_workspace.h"
#include "../../application/five_day_snapshot.h"
#include "../../application/ics_structure.h"
#include "../../application/ics_source_stage.h"
#include "../../application/ics_typed_values.h"
#include "../../application/ics_event_local.h"
#include "../../application/ics_timezone_local.h"

static_assert(sizeof(IcsTimezoneLocal::Report) == 16 && alignof(IcsTimezoneLocal::Report) == 4,
              "target timezone local report layout");

static_assert(sizeof(IcsEventLocal::Report) == 16 && alignof(IcsEventLocal::Report) == 4,
              "target VEVENT local report layout");

static_assert(sizeof(IcsTypedValues::Date) == 4 && sizeof(IcsTypedValues::DateTime) == 8 &&
              sizeof(IcsTypedValues::Duration) == 48 && sizeof(IcsTypedValues::Period) == 72 &&
              sizeof(IcsTypedValues::TemporalValue) == 80 && sizeof(IcsTypedValues::TemporalList) == 56,
              "target typed POD/cursor layout");

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

static_assert(sizeof(IcsSourceStage::Component) == 48 && alignof(IcsSourceStage::Component) == 8 &&
              sizeof(IcsSourceStage::Property) == 40 && alignof(IcsSourceStage::Property) == 4, "target stage layout");
static uint64_t stageClock(void*) { return 0; }
static GfxResult sourceStageScratch(uint8_t* storage, size_t bytes, void*) {
  const IcsSourceStage::Limits limits = {8,16,1024,4,128};
  const IcsStructure::Limits syntax = {512,8,8,16,4,32,64,128,16,2};
  const IcsIngestion::Limits transport = {4096,4096,1024,16,128,64,512,8,128,256,512,1000,100};
  size_t stageBytes = 0, syntaxBytes = 0;
  if (IcsSourceStage::requiredBytes(limits, stageBytes) != IcsSourceStage::Status::InProgress ||
      IcsStructure::scratchBytes(syntax, syntaxBytes) != IcsStructure::Status::InProgress ||
      stageBytes + syntaxBytes + 768 > bytes)
    return GfxResult(GfxError::InvalidArgument, "stage scratch capacity");
  IcsSourceStage stage; IcsStructure parser; IcsIngestion http;
  if (stage.initialize(storage, stageBytes, limits) != IcsSourceStage::Status::InProgress ||
      parser.initialize(storage + stageBytes, syntaxBytes, syntax, IcsSourceStage::sink, &stage) != IcsStructure::Status::InProgress ||
      http.initialize(storage + stageBytes + syntaxBytes, 768, transport, IcsStructure::lineSink, &parser, stageClock, nullptr) != IcsIngestion::Status::InProgress)
    return GfxResult(GfxError::InvalidArgument, "stage initialization");
  const char body[] = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:synthetic\r\nBEGIN:VEVENT\r\nUID:event\r\n"
      "DTSTART;TZID=Later:20261012T090000\r\nSUMMARY:Typed\\, title\r\n"
      "RDATE;VALUE=DATE:20261013,20261014\r\n"
      "DTSTAMP:20261012T080000Z\r\n"
      "END:VEVENT\r\nBEGIN:X-CUSTOM\r\nX-P:ignored\r\nEND:X-CUSTOM\r\n"
      "BEGIN:VTIMEZONE\r\nTZID:Later\r\nBEGIN:STANDARD\r\nDTSTART:20261012T020000\r\n"
      "TZOFFSETFROM:-0500\r\nTZOFFSETTO:-0400\r\nRDATE:20261013T020000,20261014T020000\r\n"
      "END:STANDARD\r\nEND:VTIMEZONE\r\nEND:VCALENDAR\r\n";
  char header[96];
  const int n = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", unsigned(sizeof(body) - 1));
  if (n <= 0 || size_t(n) >= sizeof(header) ||
      http.feed(reinterpret_cast<const uint8_t*>(header), size_t(n)) != IcsIngestion::Status::InProgress ||
      http.feed(reinterpret_cast<const uint8_t*>(body), sizeof(body) - 1) != IcsIngestion::Status::InProgress ||
      http.finish() != IcsIngestion::Status::Complete || parser.finish() != IcsStructure::Status::StructurallyComplete ||
      stage.finish(http, parser) != IcsSourceStage::Status::StagedSuccessfully)
    return GfxResult(GfxError::InvalidArgument, "stage completion");
  IcsSourceStage::View value;
  IcsTypedValues::TemporalValue temporal = {};
  IcsTypedValues::Selection selection = {};
  IcsTypedValues::PropertyView title = {};
  uint8_t decoded[16]; size_t written = 0;
  if (IcsTypedValues::decodeTemporal(stage, 3, temporal, selection).status != IcsTypedValues::Status::Ok ||
      temporal.dateTime.mode != IcsTypedValues::TimeMode::Tzid ||
      IcsTypedValues::readProperty(stage, 4, title).status != IcsTypedValues::Status::Ok ||
      IcsTypedValues::writeText(title.value, decoded, sizeof(decoded), true, written).status != IcsTypedValues::Status::Ok ||
      written != 12 || strcmp(reinterpret_cast<const char*>(decoded), "Typed, title"))
    return GfxResult(GfxError::InvalidArgument, "typed staged value");
  IcsTypedValues::TemporalList list(stage, 5, {2, 128});
  int32_t number = 0;
  if (list.next(temporal).status != IcsTypedValues::Status::Ok ||
      list.next(temporal).status != IcsTypedValues::Status::Ok ||
      list.next(temporal).status != IcsTypedValues::Status::End || !list.complete() ||
      IcsTypedValues::writeParameter(selection.tzid, decoded, sizeof(decoded), true, written).status != IcsTypedValues::Status::Ok ||
      written != 5 ||
      IcsTypedValues::decodeInteger({reinterpret_cast<const uint8_t*>("1"), 1}, number).status != IcsTypedValues::Status::Ok ||
      IcsTypedValues::decodeUtcOffset({reinterpret_cast<const uint8_t*>("+0100"), 5}, number).status != IcsTypedValues::Status::Ok)
    return GfxResult(GfxError::InvalidArgument, "typed list/numeric value");
  const IcsEventLocal::Report local = IcsEventLocal::validate(stage, 0, 1, {2, 8, 512});
  const uint32_t pending = IcsEventLocal::SourceValidationRequired | IcsEventLocal::IdentityReconciliationPending |
      IcsEventLocal::RecurrenceValidationPending | IcsEventLocal::TimezoneResolutionPending;
  if (local.status != IcsEventLocal::Status::LocalChecksPassed || (local.deferred & pending) != pending)
    return GfxResult(GfxError::InvalidArgument, "VEVENT local checks");
  const IcsTimezoneLocal::Limits zoneLimits = {1, 4, 64, 2, 31};
  const auto zone = IcsTimezoneLocal::validateTimezone(stage, 0, 3, zoneLimits);
  const auto onset = IcsTimezoneLocal::validateObservance(stage, 3, 4, zoneLimits);
  const uint32_t zonePending = IcsTimezoneLocal::SourceValidationRequired | IcsTimezoneLocal::TransitionResolutionPending;
  if (zone.status != IcsTimezoneLocal::Status::LocalChecksPassed ||
      zone.deferred != (zonePending | IcsTimezoneLocal::TimezoneIdentityReconciliationPending) ||
      onset.status != IcsTimezoneLocal::Status::LocalChecksPassed ||
      onset.deferred != (zonePending | IcsTimezoneLocal::RecurrenceValidationPending))
    return GfxResult(GfxError::InvalidArgument, "timezone local checks");
  return stage.value(2, value) == IcsSourceStage::Read::Ok && value.size == 5 &&
      stage.payload(2) == IcsSourceStage::Read::PayloadProjectedOut && stage.counts().ignoredOpaqueProperties == 1
      ? GfxResult::success() : GfxResult(GfxError::InvalidArgument, "stage inspection");
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
  if (!workspace.withScratch(0, 8192, sourceStageScratch).ok()) return;
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
