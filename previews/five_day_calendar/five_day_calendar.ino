// Deterministic EXAMPLE DATA only; no production acquisition or credentials.
#include <Seeed_GFX.h>
#include "../../application/psram_workspace.h"
#include "../../application/five_day_posix_time.h"
#include "../../application/five_day_window.h"
#include "../../application/ics_nonrecurring_snapshot.h"

#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(ARDUINO_XIAO_ESP32S3_PLUS)
#error "Select XIAO ESP32-S3 Plus with OPI PSRAM"
#endif

static FiveDaySnapshot snapshot;
static Ee02PsramWorkspace workspace;
// Preview configuration only. Production timezone/configuration remains separate.
static FiveDayPosixTime::Context previewZone = {"CET-1CEST,M3.5.0,M10.5.0/3"};
static FiveDayRenderer::Report rendering;
static bool renderingAttempted = false;

// Provisional preview budgets, not final device operating capacities.
static uint64_t sourceClock(void*) { return 0; } // No elapsed time during deterministic replay.
static const char exampleIcs[] =
  "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:EE02 deterministic example\r\nCALSCALE:GREGORIAN\r\n"
  "METHOD:PUBLISH\r\nX-WR-CALNAME:Published example\r\nX-WR-CALDESC:Public demonstration\r\nX-WR-TIMEZONE:Europe/Berlin\r\n"
  // Deliberately unordered; the repeated 02:30 labels refer to distinct UTC instants.
  "BEGIN:VEVENT\r\nUID:second\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261025T013000Z\r\nDTEND:20261025T014500Z\r\nSUMMARY;LANGUAGE=en:Second 02:30 - standard time\r\n"
  "CREATED:20261017T120000Z\r\nLAST-MODIFIED:20261018T120000Z\r\nSEQUENCE:0\r\nSTATUS:CONFIRMED\r\nTRANSP:OPAQUE\r\nCLASS:PUBLIC\r\n"
  "DESCRIPTION:Published example notification\r\nLOCATION:Example room\r\nURL:https://example.invalid/calendar\r\n"
  "ATTACH;VALUE=URI:https://example.invalid/attachment\r\n"
  "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\nDESCRIPTION:Notification not executed\r\nEND:VALARM\r\nEND:VEVENT\r\n"
  "BEGIN:VEVENT\r\nUID:first\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261025T003000Z\r\nDTEND:20261025T004500Z\r\nSUMMARY:First 02:30 - summer time\r\nEND:VEVENT\r\n"
  "BEGIN:VEVENT\r\nUID:midnight\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261023T210000Z\r\nDTEND:20261023T230000Z\r\nSUMMARY:Cross-midnight handover\r\nEND:VEVENT\r\n"
  "BEGIN:VEVENT\r\nUID:appointment\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261023T090000Z\r\nDTEND:20261023T100000Z\r\nSUMMARY:Example appointment\r\nEND:VEVENT\r\n"
  "BEGIN:VEVENT\r\nUID:point\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261026T150000Z\r\nSUMMARY:Point reminder\r\nEND:VEVENT\r\n"
  "BEGIN:VEVENT\r\nUID:last\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261027T160000Z\r\nDTEND:20261027T170000Z\r\nSUMMARY:Last-day review\r\nEND:VEVENT\r\n"
  "BEGIN:VEVENT\r\nUID:holiday\r\nDTSTAMP:20261017T120000Z\r\nDTSTART;VALUE=DATE:20261023\r\nSUMMARY:All day: example holiday\r\nEND:VEVENT\r\n"
  "BEGIN:VEVENT\r\nUID:visit\r\nDTSTAMP:20261017T120000Z\r\nDTSTART;VALUE=DATE:20261024\r\nDTEND;VALUE=DATE:20261027\r\nSUMMARY:Multiday example visit\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
static const char incompleteIcs[] =
  "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:EE02 incomplete example\r\n"
  "BEGIN:VEVENT\r\nUID:unsupported\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261023T090000Z\r\n"
  "STATUS:CANCELLED\r\nSUMMARY:Unsupported cancellation example\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

static GfxResult populateFromIcs(uint8_t* bytes, size_t capacity, void*) {
  const IcsSourceStage::Limits stagingLimits = {32,128,16384,32,4096};
  const IcsStructure::Limits syntaxLimits = {4096,32,32,64,8,64,128,512,32,1};
  const IcsIngestion::Limits inputLimits = {32768,16384,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
  const IcsNonrecurringSnapshot::Limits producerLimits = {
    {32,1,16,0,0,8,{8,32,4096},{8,8,4096,32,4096}},4096,120};
  size_t stageBytes = 0, parserBytes = 0;
  if (IcsSourceStage::requiredBytes(stagingLimits, stageBytes) != IcsSourceStage::Status::InProgress ||
      IcsStructure::scratchBytes(syntaxLimits, parserBytes) != IcsStructure::Status::InProgress ||
      stageBytes > capacity || parserBytes > capacity - stageBytes ||
      6144 > capacity - stageBytes - parserBytes || 8192 > capacity - stageBytes - parserBytes - 6144)
    return GfxResult(GfxError::InvalidArgument, "preview scratch capacity");
  // Each accepted initializer owns alignment within its checked slice.
  uint8_t* parserStorage = bytes + stageBytes;
  uint8_t* inputStorage = parserStorage + parserBytes;
  uint8_t* textStorage = inputStorage + 6144;
  const size_t liveBytes = stageBytes + parserBytes + 6144;
  for (size_t source = 0; source < 2; ++source) {
    IcsSourceStage stage; IcsStructure structure; IcsIngestion input;
    if (stage.initialize(bytes, stageBytes, stagingLimits) != IcsSourceStage::Status::InProgress ||
        structure.initialize(parserStorage, parserBytes, syntaxLimits, IcsSourceStage::sink, &stage) != IcsStructure::Status::InProgress ||
        input.initialize(inputStorage, 6144, inputLimits, IcsStructure::lineSink, &structure, sourceClock, nullptr) != IcsIngestion::Status::InProgress)
      return GfxResult(GfxError::InvalidArgument, "preview ICS initialization");
    const char* body = source == 0 ? exampleIcs : incompleteIcs;
    const size_t length = strlen(body);
    char header[96];
    const int n = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", unsigned(length));
    if (n < 0 || size_t(n) >= sizeof(header)) return GfxResult(GfxError::InvalidArgument, "preview HTTP header");
    auto fed = input.feed(reinterpret_cast<const uint8_t*>(header), size_t(n));
    for (size_t i = 0; i < length && fed == IcsIngestion::Status::InProgress; ++i)
      fed = input.feed(reinterpret_cast<const uint8_t*>(body + i), 1);
    const auto httpStatus = input.finish();
    const auto structureStatus = structure.finish();
    const auto staged = stage.finish(input, structure);
    const auto produced = IcsNonrecurringSnapshot::publish(stage, snapshot, source, producerLimits,
        textStorage, 8192, {bytes, liveBytes});
    Serial.printf("Example source=%u HTTP=%u structure=%u stage=%u producer=%u records=%u outside=%u\n",
        unsigned(source), unsigned(httpStatus), unsigned(structureStatus), unsigned(staged), unsigned(produced.status),
        unsigned(produced.publishedOccurrences), unsigned(produced.outsideWindowOccurrences));
    input.release(); structure.release(); stage.release(); // All borrowed users end before slice reuse.
    if ((source == 0 && produced.status != IcsNonrecurringSnapshot::Status::SourceCommitted) ||
        (source == 1 && (produced.status != IcsNonrecurringSnapshot::Status::UnsupportedProfile ||
         snapshot.source(1)->outcome != FiveDaySnapshot::Outcome::Incomplete)))
      return GfxResult(GfxError::InvalidArgument, "unexpected example source outcome");
  }
  return GfxResult::success();
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
  FiveDaySnapshot::Window window = {};
  // Fixed EXAMPLE now, explicitly trusted for this deterministic replay only.
  const auto built = FiveDayWindow::build(1792746000, true, previewZone.configuredTimezone, window);
  const int64_t expected[] = {1792706400,1792792800,1792879200,1792969200,1793055600,1793142000};
  bool equivalent = built == FiveDayWindow::Status::Ok && window.firstCivilDay == 20749 && !window.reserved;
  for (size_t i = 0; i < 6 && equivalent; ++i) equivalent = window.utcBoundaries[i] == expected[i];
  if (!equivalent) { Serial.printf("Example window failed: status=%u\n", unsigned(built)); return; }
  const FiveDaySnapshot::Text labels[] = {{"Example personal",16},{"Example team",12}};
  if (snapshot.initialize(window, 32, 4096, labels, 2) != FiveDaySnapshot::Result::Ok ||
      !workspace.allocate(2097152).ok()) { Serial.println("Preview PSRAM allocation failed"); return; }
  if (!workspace.withScratch(0, workspace.capacity(), populateFromIcs).ok()) {
    Serial.println("Example ICS pipeline failed"); return;
  }
  // Demonstrate independent snapshot text/records before borrowing the framebuffer.
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
