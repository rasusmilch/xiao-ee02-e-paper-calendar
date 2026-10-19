// DEVELOPMENT DEMONSTRATION: calendar only; target/physical behavior UNVERIFIED.
// Opt-in live network activity on YOUR board only. Never used by production or
// the deterministic preview. No retries, scheduler, deep sleep or mock panels.
#include <WiFi.h>
#include <esp_sntp.h>
#include <atomic>
#include "../../application/five_day_calendar_refresh.h"
#include "../../application/five_day_posix_time.h"
#include "../../application/ics_https_arduino.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(ARDUINO_XIAO_ESP32S3_PLUS)
#error "Select XIAO ESP32-S3 Plus with OPI PSRAM; Arduino-ESP32 3.3.12, Core Debug Level None"
#endif
#if __has_include("live_demo_config.h")
#include "live_demo_config.h"
#else
#error "Live demo disabled: copy live_demo_config.example.h to ignored live_demo_config.h and configure locally"
#endif

static FiveDaySnapshot snapshot;
static Ee02PsramWorkspace workspace;
static IcsHttpsArduino::Adapter secure;
static FiveDayPosixTime::Context displayZone={LiveDemoConfig::posixTimezone};
static std::atomic<bool> freshSync(false);
static FiveDayRenderer::Report rendering;
static bool renderingAttempted=false;

// The inspected immediate-mode SNTP implementation updates system UTC before
// notification. A session-local atomic bridges the TCP/IP callback and setup().
static void synchronizedTime(struct timeval*) { freshSync.store(true,std::memory_order_release); }
static void stopTimeSync() {
  if(esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_set_time_sync_notification_cb(nullptr);
}
static bool networkOff() {
  const auto io=secure.transport(); io.close(io.context);
  stopTimeSync();
  WiFi.disconnect(false,false); // Do not erase stored station configuration.
  const bool stopped=WiFi.mode(WIFI_OFF);
  return stopped && WiFi.getMode()==WIFI_OFF;
}
static bool waiting(uint64_t start,uint64_t limit) {
  const uint64_t now=IcsHttpsArduino::Adapter::monotonicMs(nullptr);
  return limit && now!=UINT64_MAX && now>=start && now-start<limit;
}
static IcsHttpsSource::Limits acquisitionLimits() {
  // Provisional per-feed evaluation budgets. No total acquisition deadline.
  return {2048,253,2048,4096,512,{10000,15,10000},
    {1048576,983040,16384,64,2048,512,2048,32,2048,8192,8192,0,15000},
    {8192,64,64,128,16,256,8192,16384,512,4}, {512,4096,524288,512,65536},
    {{512,4,256,128,256,512,{512,512,8192},{512,512,8192,256,8192}},8192,65536}};
}
static void sourceResult(const FiveDayCalendarRefresh::SourceResult& r,void*) {
  // Indices and status codes only: never labels, hosts, URLs or event contents.
  Serial.printf("Source config=%u snapshot=%u outcome=%u acquisition=%u status=%u\n",
    unsigned(r.configurationIndex),unsigned(r.snapshotIndex),unsigned(r.outcome),
    unsigned(r.acquisitionInvoked),r.acquisitionInvoked?unsigned(r.acquisition.status):0U);
}
static GfxResult poison(uint8_t* bytes,size_t n,void*) { memset(bytes,0xa5,n); return GfxResult::success(); }
static GfxResult draw(Seeed_GFX& display,void*) {
  display.setRotation(1);
  display.fillScreen(TFT_WHITE); // Demo composition only; renderer stays bounded.
  display.drawLine(1044,10,1044,1180,TFT_BLACK);
  display.drawLine(10,1044,1034,1044,TFT_BLACK);
  display.setFreeFont(nullptr);
  const char* notice="LIVE ICS CALENDAR DEMO - DEVELOPMENT";
  for(size_t i=0;notice[i];++i) display.drawChar(10+int32_t(i)*12,1060,notice[i],TFT_RED,TFT_WHITE,2);
  renderingAttempted=true;
  rendering=FiveDayRenderer::render(display,snapshot,{10,10,1025,1025},{FiveDayPosixTime::convert,&displayZone});
  return rendering.status==FiveDayRenderer::Status::Drawn?GfxResult::success():
    GfxResult(GfxError::InvalidArgument,"live calendar rendering failed");
}
static void releaseHealthy() {
  if(workspace.state()==Ee02PsramWorkspace::State::Scratch) workspace.release();
  else if(workspace.state()==Ee02PsramWorkspace::State::Retained)
    Serial.println("Workspace retained: no reuse/free after uncertain panel shutdown");
  const auto result=snapshot.release();
  Serial.printf("Snapshot release=%u\n",unsigned(result));
}
void setup() {
  Serial.begin(115200);
  Serial.println("LIVE ICS CALENDAR DEVELOPMENT DEMONSTRATION - target unverified");
  if(!LiveDemoConfig::configured || !*LiveDemoConfig::wifiSsid || !*LiveDemoConfig::posixTimezone ||
     !*LiveDemoConfig::ntpServer || !*LiveDemoConfig::caPem || !LiveDemoConfig::wifiWaitMs || !LiveDemoConfig::ntpWaitMs) {
    Serial.println("Demo configuration missing or opt-in disabled"); return;
  }
  WiFi.persistent(false); // No routine configuration/event writes to NVS.
  WiFi.mode(WIFI_STA);
  uint64_t start=IcsHttpsArduino::Adapter::monotonicMs(nullptr);
  WiFi.begin(LiveDemoConfig::wifiSsid,LiveDemoConfig::wifiPassword);
  while(WiFi.status()!=WL_CONNECTED && waiting(start,LiveDemoConfig::wifiWaitMs)) delay(20);
  if(WiFi.status()!=WL_CONNECTED) { Serial.println("Demo prerequisite failed: Wi-Fi"); networkOff(); return; }

  // Stop any prior service before installing a fresh-session notification gate.
  stopTimeSync();
  freshSync.store(false,std::memory_order_release);
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  esp_sntp_set_time_sync_notification_cb(synchronizedTime); // Before initiation.
  start=IcsHttpsArduino::Adapter::monotonicMs(nullptr);
  configTime(0,0,LiveDemoConfig::ntpServer);
  while(!freshSync.load(std::memory_order_acquire) && waiting(start,LiveDemoConfig::ntpWaitMs)) delay(20);
  const bool trusted=freshSync.load(std::memory_order_acquire);
  stopTimeSync(); // Freeze this session's time context before acquisition/display.
  if(!trusted) { Serial.println("Demo prerequisite failed: no fresh NTP notification"); networkOff(); return; }
  int64_t nowUtc=0;
  if(!FiveDayLocalTime::fromStamp(time(nullptr),nowUtc) || setenv("TZ",LiveDemoConfig::posixTimezone,1)!=0) {
    Serial.println("Demo prerequisite failed: time/TZ conversion"); networkOff(); return;
  }
  tzset();
  // Validate context before allocating. The coordinator builds the same window
  // immediately before initializing its one independent snapshot.
  FiveDaySnapshot::Window checked={};
  const auto built=FiveDayWindow::build(nowUtc,trusted,LiveDemoConfig::posixTimezone,checked);
  if(built!=FiveDayWindow::Status::Ok) { Serial.printf("Demo window failed=%u\n",unsigned(built)); networkOff(); return; }
  if(!workspace.allocate(LiveDemoConfig::workspaceBytes).ok()) {
    Serial.println("Demo workspace allocation failed"); networkOff(); return;
  }
  const FiveDayCalendarRefresh::Limits limits={LiveDemoConfig::maxConfiguredSources,
    LiveDemoConfig::recordCapacity,LiveDemoConfig::textCapacity,acquisitionLimits()};
  const auto transport=secure.transport();
  const auto report=FiveDayCalendarRefresh::refresh(nowUtc,trusted,LiveDemoConfig::posixTimezone,workspace,snapshot,
    LiveDemoConfig::sources,sizeof(LiveDemoConfig::sources)/sizeof(LiveDemoConfig::sources[0]),limits,
    WiFi.status()==WL_CONNECTED,{reinterpret_cast<const uint8_t*>(LiveDemoConfig::caPem),sizeof(LiveDemoConfig::caPem)},
    transport,IcsHttpsArduino::Adapter::monotonicMs,nullptr,sourceResult);
  if(!networkOff()) { // All sockets, SNTP and Wi-Fi finish BEFORE framebuffer borrowing.
    Serial.println("Demo network shutdown unconfirmed; no display refresh"); releaseHealthy(); return;
  }
  Serial.printf("Refresh status=%u error=%u selected=%u complete=%u incomplete=%u pending=%u records=%u\n",
    unsigned(report.status),unsigned(report.error),unsigned(report.selectedSources),unsigned(report.completeSources),
    unsigned(report.incompleteSources),unsigned(report.pendingSources),unsigned(report.publishedOccurrences));
  if(!report.acquisitionComplete) { Serial.println("Refresh interrupted/admission failed; no display refresh"); releaseHealthy(); return; }
  if(!workspace.withScratch(0,workspace.capacity(),poison).ok()) { releaseHealthy(); return; }
  const auto result=workspace.render(draw); // Exactly one e-paper refresh at most.
  Serial.printf("Renderer called=%u status=%u; render/refresh=%u shutdown checked=%u result=%u\n",
    unsigned(renderingAttempted),unsigned(rendering.status),unsigned(result.operation.error),
    unsigned(result.shutdownChecked),unsigned(result.shutdown.error));
  releaseHealthy(); // Retained storage deliberately remains allocated and alive.
}
void loop() {}
