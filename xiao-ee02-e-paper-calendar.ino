// Canonical EE02 application. Target/hardware behavior remains unverified.
// Private provisional configuration; fresh SNTP on every wake until RTC policy is implemented.
#include <WiFi.h>
#include <esp_sntp.h>
#include <atomic>
#include <esp_sleep.h>
#include "application/five_day_calendar_refresh.h"
#include "application/five_day_posix_time.h"
#include "application/ics_https_arduino.h"
#include "application/weather_acquisition.h"
#include "application/battery_measurement.h"
#include "application/five_day_dashboard_renderer.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(ARDUINO_XIAO_ESP32S3_PLUS)
#error "Select XIAO ESP32-S3 Plus with OPI PSRAM; Arduino-ESP32 3.3.12, Core Debug Level None"
#endif
#ifndef EE02_DEVICE_CONFIG_H
#if __has_include("device_config.h")
#include "device_config.h"
#else
#error "Application disabled: copy device_config.example.h to ignored device_config.h and configure locally"
#endif
#endif

static FiveDaySnapshot snapshot;
static Ee02PsramWorkspace workspace;
static IcsHttpsArduino::Adapter secure;
static FiveDayPosixTime::Context displayZone={DeviceConfig::posixTimezone};
static std::atomic<bool> freshSync(false);
static FiveDayDashboardRenderer::Report rendering;
static FiveDayDashboardRenderer::Battery battery={false,0};
static int64_t observedUtc=0;
static bool renderingAttempted=false;

// The inspected immediate-mode SNTP implementation updates system UTC before
// notification. A session-local atomic bridges the TCP/IP callback and setup().
static void synchronizedTime(struct timeval*) { freshSync.store(true,std::memory_order_release); }
static void stopTimeSync() {
  if(esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_set_time_sync_notification_cb(nullptr);
  freshSync.store(false,std::memory_order_release);
}
static bool networkOff() {
  const auto io=secure.transport(); io.close(io.context);
  stopTimeSync();
  WiFi.disconnect(false,false); // Do not erase stored station configuration.
  const bool stopped=WiFi.mode(WIFI_OFF);
  return stopped && WiFi.getMode()==WIFI_OFF && !esp_sntp_enabled();
}
static bool waiting(uint64_t start,uint64_t limit) {
  const uint64_t now=IcsHttpsArduino::Adapter::monotonicMs(nullptr);
  return limit && now!=UINT64_MAX && now>=start && now-start<limit;
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
  display.fillScreen(TFT_WHITE); // One complete composition; panel update belongs to the workspace.
  renderingAttempted=true;
  const FiveDayDashboardRenderer::Inputs input={&weatherData,forecastData,FORECAST_COUNT,
    dailyForecastData,DAILY_FCST_COUNT,battery,true,observedUtc};
  rendering=FiveDayDashboardRenderer::render(display,snapshot,{FiveDayPosixTime::convert,&displayZone},
    input,FiveDayDashboardRenderer::ee02PreviewGeometry());
  if(rendering.status!=FiveDayDashboardRenderer::Status::Drawn)
    return GfxResult(GfxError::InvalidArgument,"dashboard rendering failed");
  return GfxResult::success();
}
// Transient diagnostics only: no scheduler, retained counter or wake-state machine.
enum class CycleStatus { NotStarted, MissingConfiguration, InvalidInterval, WifiFailure,
  TimeFailure, InvalidTimezone, WorkspaceFailure, NetworkShutdownFailure, CalendarInterruption, ScratchFailure,
  RendererFailure, PanelOperationFailure, PanelShutdownFailure, Refreshed };
enum class SleepStatus { NotAttempted, UnsafeShutdown, ReleaseFailure, TimerWakeFailure, Entering };
static CycleStatus cycleStatus=CycleStatus::NotStarted;
static SleepStatus sleepStatus=SleepStatus::NotAttempted;
static esp_err_t timerWakeResult=ESP_OK;

// Explicit declarations avoid Arduino auto-prototypes preceding local types.
template<size_t N> static bool configString(const char (&text)[N],bool allowEmpty=false);
static void finishCycle(CycleStatus status,uint64_t micros,bool networkSafe,bool panelSafe=true);
static void abortCycle(CycleStatus status,uint64_t micros);

template<size_t N> static bool configString(const char (&text)[N],bool allowEmpty) {
  return (allowEmpty || N>1) && memchr(text,0,N)==text+N-1;
}
static bool sleepInterval(uint64_t seconds,uint64_t& micros) {
  if(!seconds || seconds>UINT64_MAX/1000000) return false;
  const uint64_t candidate=seconds*1000000;
  // IDF's timer preparation uses a signed duration. The timer API separately
  // checks its hardware-counter limit against the active slow-clock frequency.
  if(candidate>uint64_t(INT64_MAX)) return false;
  micros=candidate; return true;
}
static bool releaseHealthy() {
  const auto state=workspace.state();
  if(state!=Ee02PsramWorkspace::State::Scratch && state!=Ee02PsramWorkspace::State::Unallocated &&
     state!=Ee02PsramWorkspace::State::Retained) return false;
  if(snapshot.release()!=FiveDaySnapshot::Result::Ok) return false;
  if(state==Ee02PsramWorkspace::State::Retained) {
    Serial.println("Workspace retained: no reuse/free or sleep after uncertain panel operation");
    return false;
  }
  return workspace.release().ok();
}
static void finishCycle(CycleStatus status,uint64_t micros,bool networkSafe,bool panelSafe) {
  cycleStatus=status;
  Serial.printf("Cycle status=%u\n",unsigned(status));
  const bool released=releaseHealthy();
  if(!networkSafe || WiFi.getMode()!=WIFI_OFF || esp_sntp_enabled() || !panelSafe) {
    sleepStatus=SleepStatus::UnsafeShutdown;
    if(!networkSafe) Serial.println("Network shutdown unconfirmed");
    if(!panelSafe) Serial.println("Panel shutdown/operation unconfirmed");
    Serial.println("Sleep refused: resource shutdown unconfirmed"); return;
  }
  if(!released) {
    sleepStatus=SleepStatus::ReleaseFailure;
    Serial.println("Sleep refused: resources still owned or active"); return;
  }
  timerWakeResult=esp_sleep_enable_timer_wakeup(micros);
  if(timerWakeResult!=ESP_OK) {
    sleepStatus=SleepStatus::TimerWakeFailure;
    Serial.printf("Timer wake configuration failed: %d\n",int(timerWakeResult)); return;
  }
  sleepStatus=SleepStatus::Entering;
  Serial.println("Entering configured timer deep sleep");
  Serial.flush();
  esp_deep_sleep_start();
}
static void abortCycle(CycleStatus status,uint64_t micros) {
  const bool stopped=networkOff();
  finishCycle(status,micros,stopped);
}
void setup() {
  Serial.begin(115200);
  Serial.println("EE02 dashboard application - target unverified");
  cycleStatus=CycleStatus::NotStarted; sleepStatus=SleepStatus::NotAttempted;
  timerWakeResult=ESP_OK; renderingAttempted=false;
  if(!DeviceConfig::configured) {
    cycleStatus=CycleStatus::MissingConfiguration;
    Serial.println("Application configuration missing or opt-in disabled"); return;
  }
  uint64_t sleepUs=0;
  if(!sleepInterval(DeviceConfig::refreshSeconds,sleepUs)) {
    cycleStatus=CycleStatus::InvalidInterval;
    Serial.println("Application refresh interval invalid"); return;
  }
  if(!configString(DeviceConfig::wifiSsid) || !configString(DeviceConfig::wifiPassword,true) ||
     !configString(DeviceConfig::ntpServer) || !DeviceConfig::wifiWaitMs || !DeviceConfig::ntpWaitMs) {
    cycleStatus=CycleStatus::MissingConfiguration;
    Serial.println("Application configuration invalid"); return;
  }
  if(!configString(DeviceConfig::posixTimezone)) {
    abortCycle(CycleStatus::InvalidTimezone,sleepUs); return;
  }
  WiFi.persistent(false); // No routine configuration/event writes to NVS.
  WiFi.mode(WIFI_STA);
  uint64_t start=IcsHttpsArduino::Adapter::monotonicMs(nullptr);
  WiFi.begin(DeviceConfig::wifiSsid,DeviceConfig::wifiPassword);
  while(WiFi.status()!=WL_CONNECTED && waiting(start,DeviceConfig::wifiWaitMs)) delay(20);
  if(WiFi.status()!=WL_CONNECTED) { Serial.println("Wi-Fi prerequisite failed"); abortCycle(CycleStatus::WifiFailure,sleepUs); return; }

  // Stop any prior service before installing a fresh-session notification gate.
  stopTimeSync();
  freshSync.store(false,std::memory_order_release);
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  esp_sntp_set_time_sync_notification_cb(synchronizedTime); // Before initiation.
  start=IcsHttpsArduino::Adapter::monotonicMs(nullptr);
  configTime(0,0,DeviceConfig::ntpServer);
  while(!freshSync.load(std::memory_order_acquire) && waiting(start,DeviceConfig::ntpWaitMs)) delay(20);
  const bool trusted=freshSync.load(std::memory_order_acquire);
  stopTimeSync(); // Freeze this session's time context before acquisition/display.
  if(!trusted) { Serial.println("Fresh time prerequisite failed"); abortCycle(CycleStatus::TimeFailure,sleepUs); return; }
  int64_t nowUtc=0;
  if(!FiveDayLocalTime::fromStamp(time(nullptr),nowUtc)) {
    abortCycle(CycleStatus::TimeFailure,sleepUs); return;
  }
  if(setenv("TZ",DeviceConfig::posixTimezone,1)!=0) {
    abortCycle(CycleStatus::InvalidTimezone,sleepUs); return;
  }
  tzset();
  // Validate context before allocating. The coordinator builds the same window
  // immediately before initializing its one independent snapshot.
  FiveDaySnapshot::Window checked={};
  const auto built=FiveDayWindow::build(nowUtc,trusted,DeviceConfig::posixTimezone,checked);
  if(built!=FiveDayWindow::Status::Ok) { Serial.printf("Window failed=%u\n",unsigned(built)); abortCycle(CycleStatus::InvalidTimezone,sleepUs); return; }
  if(!workspace.allocate(DeviceConfig::workspaceBytes).ok()) {
    Serial.println("Workspace allocation failed"); abortCycle(CycleStatus::WorkspaceFailure,sleepUs); return;
  }
  observedUtc=nowUtc;
  if(DeviceConfig::weatherEnabled) {
    const WeatherHttps::Trust trust={trusted,DeviceConfig::weatherCaPem,sizeof(DeviceConfig::weatherCaPem)};
    const WeatherHttps::Request request={DeviceConfig::weatherLocation,
      WeatherHttps::boundedLength(DeviceConfig::weatherLocation,sizeof(DeviceConfig::weatherLocation)),
      DeviceConfig::weatherApiKey,
      WeatherHttps::boundedLength(DeviceConfig::weatherApiKey,sizeof(DeviceConfig::weatherApiKey))};
    const bool current=fetchWeatherData(trust,request);
    const bool shortForecast=fetchForecastData(trust,request);
    const bool daily=fetchDailyForecastData(trust,request);
    Serial.printf("Weather current=%u short=%u daily=%u\n",unsigned(current),unsigned(shortForecast),unsigned(daily));
  }
  const FiveDayCalendarRefresh::Limits limits={DeviceConfig::maxConfiguredSources,
    DeviceConfig::recordCapacity,DeviceConfig::textCapacity,DeviceConfig::calendarLimits};
  const auto transport=secure.transport();
  const auto report=FiveDayCalendarRefresh::refresh(nowUtc,trusted,DeviceConfig::posixTimezone,workspace,snapshot,
    DeviceConfig::sources,sizeof(DeviceConfig::sources)/sizeof(DeviceConfig::sources[0]),limits,
    WiFi.status()==WL_CONNECTED,{reinterpret_cast<const uint8_t*>(DeviceConfig::caPem),sizeof(DeviceConfig::caPem)},
    transport,IcsHttpsArduino::Adapter::monotonicMs,nullptr,sourceResult);
  const int millivolts=readBatteryMillivolts(); // One sample; divider is disabled on return.
  battery={millivolts>0,millivolts>0?voltageToPercent(millivolts):0};
  const bool networkSafe=networkOff(); // All network/parser users finish BEFORE graphics.
  if(!networkSafe) {
    finishCycle(CycleStatus::NetworkShutdownFailure,sleepUs,false); return;
  }
  Serial.printf("Refresh status=%u error=%u selected=%u complete=%u incomplete=%u pending=%u records=%u\n",
    unsigned(report.status),unsigned(report.error),unsigned(report.selectedSources),unsigned(report.completeSources),
    unsigned(report.incompleteSources),unsigned(report.pendingSources),unsigned(report.publishedOccurrences));
  if(!report.acquisitionComplete) { finishCycle(CycleStatus::CalendarInterruption,sleepUs,true); return; }
  if(!workspace.withScratch(0,workspace.capacity(),poison).ok()) { finishCycle(CycleStatus::ScratchFailure,sleepUs,true); return; }
  const auto result=workspace.render(draw); // Exactly one e-paper refresh at most.
  Serial.printf("Renderer called=%u status=%u; render/refresh=%u shutdown checked=%u result=%u\n",
    unsigned(renderingAttempted),unsigned(rendering.status),unsigned(result.operation.error),
    unsigned(result.shutdownChecked),unsigned(result.shutdown.error));
  const bool panelSafe=result.shutdownChecked && result.shutdown.ok() &&
    workspace.state()==Ee02PsramWorkspace::State::Scratch;
  const CycleStatus status=!result.shutdownChecked || !result.shutdown.ok() ? CycleStatus::PanelShutdownFailure :
    workspace.state()==Ee02PsramWorkspace::State::Retained ? CycleStatus::PanelOperationFailure :
    renderingAttempted && rendering.status!=FiveDayDashboardRenderer::Status::Drawn ? CycleStatus::RendererFailure :
    !result.operation.ok() ? CycleStatus::PanelOperationFailure : CycleStatus::Refreshed;
  finishCycle(status,sleepUs,true,panelSafe);
}
void loop() {}
