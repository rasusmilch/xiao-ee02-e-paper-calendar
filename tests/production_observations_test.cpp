// No credentials.h, TFT_eSPI.h or legacy weather.h/battery.h. The API key is
// synthetic configuration supplied by this translation unit, never a real secret.
static const char* OPENWEATHER_API_KEY="synthetic-nonsecret-fixture";
#include "../application/weather_acquisition.h"
#include "../application/battery_measurement.h"
#include "../application/weather_acquisition.h" // Same guarded observation storage.
#include "../application/five_day_dashboard_renderer.h"
#include "../application/five_day_posix_time.h"
#include "../application/five_day_window.h"
#include <cassert>
#include <algorithm>
#include <vector>

namespace ObservationHost {
Action actions[16];size_t actionCount=0;int adc=0;SerialSink serial;
}
bool psramFound(){return true;}
void* ps_malloc(size_t n){return std::malloc(n);}
namespace {
bool has(const Seeed_GFX& gfx,const std::string& wanted,FiveDayRenderer::Region region={0,0,1600,1200}){
 for(const auto& glyph:gfx.glyphs){if(glyph.y<region.y || glyph.y>=region.y+region.height)continue;std::vector<Seeed_GFX::Glyph> row;
  for(const auto& g:gfx.glyphs)if(g.y==glyph.y && g.x>=region.x && g.x<region.x+region.width)row.push_back(g);
  std::sort(row.begin(),row.end(),[](const Seeed_GFX::Glyph& a,const Seeed_GFX::Glyph& b){return a.x<b.x;});
  std::string text;for(const auto& g:row)text+=char(g.code);if(text.find(wanted)!=std::string::npos)return true;
 }return false;
}
void fetchEntryPoints(){
 using namespace ObservationHost;
 assert(!weatherData.valid && !forecastData[0].valid && !dailyForecastData[0].valid);
 const char* suffix[]={"weather?q=Lohr%20am%20Main,DE&appid=synthetic-nonsecret-fixture&units=metric",
   "forecast?q=Lohr%20am%20Main,DE&appid=synthetic-nonsecret-fixture&units=metric&cnt=5",
   "forecast?q=Lohr%20am%20Main,DE&appid=synthetic-nonsecret-fixture&units=metric&cnt=40"};
 const char ca[]="synthetic-not-a-certificate";
 const WeatherHttps::Trust trust={true,ca,sizeof(ca)};
 bool(*operations[])(const WeatherHttps::Trust&)={fetchWeatherData,fetchForecastData,fetchDailyForecastData};
 for(int code:{503,200})for(size_t i=0;i<3;++i){https().status=code;assert(!operations[i](trust));
  assert(https().request==std::string("https://api.openweathermap.org/data/2.5/")+suffix[i]);
  assert(!weatherData.valid && !forecastData[0].valid && !dailyForecastData[0].valid);
 }
 assert(https().begins==6 && https().requests==6 && https().ends==6 && https().caCalls==6 && https().stops==6);
 // Existing fetch failures do not clear previously valid observations. This is
 // preserved RAM behavior, not a freshness/retention guarantee for the redesign.
 weatherData={true,"01d","Prior synthetic observation","Prior fetch",10,60,0,1};
 forecastData[0]={true,"12:00","01d",12,6};dailyForecastData[0]={true,"Friday","01d","Prior clear",12,6};
 const auto prior=weatherData;const auto forecast=forecastData[0];const auto daily=dailyForecastData[0];
 https().status=503;
 for(auto operation:operations)assert(!operation(trust));
 assert(!std::memcmp(&prior,&weatherData,sizeof(prior)) && !std::memcmp(&forecast,&forecastData[0],sizeof(forecast)) &&
     !std::memcmp(&daily,&dailyForecastData[0],sizeof(daily)));
 assert(https().begins==9 && https().requests==9 && https().ends==9 && https().caCalls==9 && https().stops==9);
}
void curveAndSampling(){
 using namespace ObservationHost;
 const int millivolts[]={4200,4030,3950,3870,3800,3740,3680,3610,3520,3430,3300};
 const int percentage[]={100,90,80,70,60,50,40,30,20,10,0};
 for(size_t i=0;i<11;++i)assert(voltageToPercent(millivolts[i])==percentage[i]);
 assert(voltageToPercent(0)==0 && voltageToPercent(5000)==100 && voltageToPercent(4115)==95 && voltageToPercent(3990)==85 && voltageToPercent(3365)==5);
 int last=0;for(int mv=3300;mv<=4200;++mv){const int p=voltageToPercent(mv);assert(p>=last && p>=0 && p<=100);last=p;}
 const int raw[]={0,1,2048,4095};const int expected[]={0,2,3301,6600};
 for(size_t i=0;i<4;++i){adc=raw[i];actionCount=0;assert(readBatteryMillivolts()==expected[i] && actionCount==5);
  assert(actions[0].operation==0 && actions[0].pin==A5 && actions[0].value==OUTPUT);
  assert(actions[1].operation==1 && actions[1].pin==A5 && actions[1].value==HIGH);
  assert(actions[2].operation==2 && actions[2].value==5);
  assert(actions[3].operation==3 && actions[3].pin==A0);
  assert(actions[4].operation==1 && actions[4].pin==A5 && actions[4].value==LOW);
 }
}
void dashboard(){
 namespace D=FiveDayDashboardRenderer;using namespace ObservationHost;
 const char* tz="UTC0";assert(!setenv("TZ",tz,1));tzset();FiveDaySnapshot::Window window={};
 assert(FiveDayWindow::build(1792746000,true,tz,window)==FiveDayWindow::Status::Ok);
 FiveDaySnapshot snapshot;const FiveDaySnapshot::Text labels[]={{"Synthetic",9}};
 assert(snapshot.initialize(window,2,256,labels,1)==FiveDaySnapshot::Result::Ok && snapshot.beginSource(0)==FiveDaySnapshot::Result::Ok);
 FiveDaySnapshot::Input e={};e.kind=FiveDaySnapshot::Kind::Timed;e.range.timed={1792746000,1792749600};e.title={"Shared observations",19};
 assert(snapshot.addOccurrence(0,e)==FiveDaySnapshot::Result::Ok && snapshot.completeSource(0)==FiveDaySnapshot::Result::Ok);
 weatherData={true,"01d","Synthetic clear","Supplied example time",15.0f,60,0.0f,2.0f};
 forecastData[0]={true,"12:00","02d",17,12};dailyForecastData[0]={true,"Friday","10d","Example rain",17,10};
 // Borrow the actual production arrays. No copied observations are passed.
 D::Inputs input={&weatherData,forecastData,FORECAST_COUNT,dailyForecastData,DAILY_FCST_COUNT,{false,0},true,1792746000};
 FiveDayPosixTime::Context zone={tz};
 for(int raw:{0,2040,2606}){adc=raw;actionCount=0;const int mv=readBatteryMillivolts();input.battery={mv>0,mv>0?voltageToPercent(mv):0};
  const auto originalWeather=weatherData;const auto originalRecord=*snapshot.occurrence(0);
  ForecastEntry originalForecast[FORECAST_COUNT];DailyForecastEntry originalDaily[DAILY_FCST_COUNT];
  std::memcpy(originalForecast,forecastData,sizeof(forecastData));std::memcpy(originalDaily,dailyForecastData,sizeof(dailyForecastData));
  Seeed_GFX gfx;const auto r=D::render(gfx,snapshot,{FiveDayPosixTime::convert,&zone},input,D::ee02PreviewGeometry());
  assert(r.status==D::Status::Drawn && has(gfx,"Shared observations") && has(gfx,"Temperature: 15.0 C"));
  assert(r.batteryUnavailable==(raw==0) && actionCount==5); // Renderer performs no ADC calls.
  if(raw==0)assert(has(gfx,"N/A",D::ee02PreviewGeometry().battery));
  if(raw==2040)assert(input.battery.valid && input.battery.percentage==0 && has(gfx,"0%",D::ee02PreviewGeometry().battery));
  assert(!std::memcmp(&weatherData,&originalWeather,sizeof(weatherData)) && !std::memcmp(snapshot.occurrence(0),&originalRecord,sizeof(originalRecord)));
  assert(!std::memcmp(originalForecast,forecastData,sizeof(forecastData)) && !std::memcmp(originalDaily,dailyForecastData,sizeof(dailyForecastData)));
  assert(input.weather==&weatherData && input.forecast==forecastData && input.daily==dailyForecastData);
 }
}
}
int main(){fetchEntryPoints();curveAndSampling();dashboard();std::puts("PASS: graphics-free acquisition admission/call shapes, battery curve/GPIO/ADC and direct shared-observation GFX2 composition; stand-ins only");}
