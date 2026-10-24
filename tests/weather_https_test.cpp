#include "../constants.h" // Historical default-request regression only.
// Synthetic configuration only. No credentials.h, legacy graphics or real network.
static const char* OPENWEATHER_API_KEY="synthetic-weather-key";
static const char* SSID="synthetic-network";
static const char* PASSWORD="synthetic-password";
#include "../application/weather_acquisition.h"
#include "../application/legacy_time_sync.h"
#include "../data_acquisition.h"
#include <cassert>
#include <vector>

namespace ObservationHost {
Action actions[16];size_t actionCount=0;int adc=0;SerialSink serial;
}
namespace {
using namespace ObservationHost;
using Operation=bool(*)(const WeatherHttps::Trust&,const WeatherHttps::Request&);
const WeatherHttps::Request request={WX_LOCATION,std::strlen(WX_LOCATION),OPENWEATHER_API_KEY,std::strlen(OPENWEATHER_API_KEY)};
const Operation operations[]={fetchWeatherData,fetchForecastData,fetchDailyForecastData};
const char ca[]="synthetic-not-a-real-CA";
const WeatherHttps::Trust trusted={true,ca,sizeof(ca)};
void unchangedFailure(Operation operation,const WeatherHttps::Trust& trust){
    const auto weather=weatherData;
    ForecastEntry forecast[FORECAST_COUNT];DailyForecastEntry daily[DAILY_FCST_COUNT];
    std::memcpy(forecast,forecastData,sizeof(forecast));std::memcpy(daily,dailyForecastData,sizeof(daily));
    assert(!operation(trust,request));
    assert(!std::memcmp(&weather,&weatherData,sizeof(weather)));
    assert(!std::memcmp(forecast,forecastData,sizeof(forecast)));
    assert(!std::memcmp(daily,dailyForecastData,sizeof(daily)));
}
void admission(){
    weatherData={true,"01d","Retained synthetic","Earlier",12,50,0,2};
    forecastData[0]={true,"09:00","02d",15,7};dailyForecastData[0]={true,"Friday","01d","Clear",17,9};
    const char missingNul[]={'a','b'};const char embedded[]={'a','\0','b','\0'};
    const WeatherHttps::Trust invalid[]={
        {false,ca,sizeof(ca)},{true,nullptr,4},{true,"",1},{true,ca,0},
        {true,ca,1},{true,missingNul,sizeof(missingNul)},{true,embedded,sizeof(embedded)},
        {true,ca,SIZE_MAX},{true,reinterpret_cast<const char*>(UINTPTR_MAX-1),4},
        {true,reinterpret_cast<const char*>(UINTPTR_MAX-1),2}};
    for(const auto& trust:invalid)for(auto operation:operations){https()={};unchangedFailure(operation,trust);
        assert(https().caCalls==0 && https().begins==0 && https().resolutions==0 &&
               https().connections==0 && https().requests==0 && https().stops==0);}
}
void httpOutcomes(){
    const char* targets[]={"weather?q=Lohr%20am%20Main,DE&appid=synthetic-weather-key&units=metric",
        "forecast?q=Lohr%20am%20Main,DE&appid=synthetic-weather-key&units=metric&cnt=5",
        "forecast?q=Lohr%20am%20Main,DE&appid=synthetic-weather-key&units=metric&cnt=40"};
    for(size_t i=0;i<3;++i)for(int scenario=0;scenario<7;++scenario){
        https()={};auto& state=https();
        state.beginOk=scenario!=0;state.connectOk=scenario!=1;
        state.status=scenario==2?503:scenario==3?302:scenario==4?307:200;
        state.transferResult=scenario==5?-1:0;state.payload="{}"; // JSON facade rejects; never a success proof.
        const unsigned parsed=parses();
        unchangedFailure(operations[i],trusted);
        assert(state.caCalls==1 && state.ca==ca && state.begins==1 && state.ends==1 && state.stops==1);
        assert(state.request==std::string("https://api.openweathermap.org/data/2.5/")+targets[i]);
        assert(state.order[0]==1 && state.order[1]==2 && state.order[2]==3);
        assert(state.order[state.order.size()-2]==8 && state.order.back()==9);
        assert(state.resolutions==unsigned(scenario!=0) && state.connections==unsigned(scenario!=0));
        assert(state.requests==unsigned(scenario>1)); // No retry or redirect request.
        if(scenario!=0)assert(state.host=="api.openweathermap.org" && state.port==443);
        assert(state.transfers==unsigned(scenario>=5));
        assert(parses()==parsed+unsigned(scenario==6)); // Transfer failure cannot reach parsing.
    }
}
void requestParameters(){
    const char location[]="Example City,XX",key[]="synthetic-key";
    const char badControl[]={'A','\n'},missingNul[]={'a','b'};
    const WeatherHttps::Request invalid[]={
        {nullptr,1,key,sizeof(key)-1},{location,0,key,sizeof(key)-1},
        {location,SIZE_MAX,key,sizeof(key)-1},{badControl,sizeof(badControl),key,sizeof(key)-1},
        {location,sizeof(location)-1,nullptr,4},{location,sizeof(location)-1,key,0},
        {location,sizeof(location)-1,"a&b",3},{location,sizeof(location)-1,"a\0b",3},
        {reinterpret_cast<const char*>(UINTPTR_MAX-1),2,key,sizeof(key)-1}};
    for(const auto& bad:invalid)for(auto operation:operations){https()={};
        assert(!operation(trusted,bad) && !https().begins && !https().resolutions);}
    assert(WeatherHttps::boundedLength(missingNul,sizeof(missingNul))==0);
    assert(WeatherHttps::boundedLength(key,sizeof(key))==sizeof(key)-1);
    for(size_t i=0;i<3;++i){
        std::string largeLocation(128,'A');WeatherHttps::Request input={largeLocation.data(),largeLocation.size(),key,sizeof(key)-1};
        https()={};assert(!operations[i](trusted,input) && !https().begins);
        largeLocation.assign(43,' ');input.location=largeLocation.data();input.locationBytes=largeLocation.size();
        assert(!operations[i](trusted,input) && !https().begins); // Encoding would exceed 127 bytes.
        // Independent exact URL length reference, then one byte beyond it.
        const std::string base=std::string("https://api.openweathermap.org/data/2.5/")+
            (i?"forecast":"weather")+"?q=A&appid=&units=metric"+(i==1?"&cnt=5":i==2?"&cnt=40":"");
        std::string longKey(255-base.size(),'k');input={"A",1,longKey.data(),longKey.size()};
        https()={};assert(!operations[i](trusted,input) && https().begins==1 && https().request.size()==255);
        longKey+='k';input.apiKey=longKey.data();input.apiKeyBytes=longKey.size();
        https()={};assert(!operations[i](trusted,input) && !https().begins);
    }
    const char encodedLocation[]="A &B/XX";
    https()={};assert(!fetchWeatherData(trusted,{encodedLocation,sizeof(encodedLocation)-1,key,sizeof(key)-1}));
    assert(https().request=="https://api.openweathermap.org/data/2.5/weather?q=A%20%26B%2FXX&appid=synthetic-key&units=metric");
}
void independentTrustFailures(){
    https()={};std::vector<int> order;
    const WeatherHttps::Trust rejected={false,ca,sizeof(ca)};
    const bool result=acquireDataSources(true,
        [&](){order.push_back(0);return fetchWeatherData(rejected,request);},
        [&](){order.push_back(1);return fetchForecastData(rejected,request);},
        [&](){order.push_back(2);return fetchDailyForecastData(rejected,request);},
        [&](){order.push_back(3);return true;},
        [&](const DataAcquisitionResults& r){order.push_back(4);assert(!r.weather && !r.forecast && !r.dailyForecast && r.calendar);},
        [&](){order.push_back(5);});
    assert(!result && order==std::vector<int>({0,1,2,3,4,5}) && https().connections==0);
}
void tick(unsigned long ms){
    auto& b=boot();b.elapsedMs+=unsigned(ms);
    if(b.notify && b.sntpEnabled && b.callback && b.elapsedMs>=b.notifyAfterMs){
        b.notify=false;struct timeval stamp={};b.callback(&stamp);
    }
}
void freshTime(){
    delayHook()=tick;
    boot()={};assert(syncTime());
    assert(boot().elapsedMs==1000 && boot().starts==1 && boot().registrations==1 && boot().unregistrations==1);
    assert(boot().order==std::vector<int>({1,2,3,0,4})); // Register before initiate; stop/unregister on exit.
    assert(!boot().callback && !boot().sntpEnabled && boot().disconnects==1 && boot().off==1);
    assert(!LegacyTimeSync::observed.load()); // No trust flag survives the attempt.
    // A previous successful invocation and plausible retained time are insufficient.
    boot()={};boot().notify=false;boot().staleTimeValid=true;
    assert(!syncTime() && boot().elapsedMs==10000 && boot().starts==1 && !boot().callback && !boot().sntpEnabled);
    boot()={};boot().wifiReady=false;
    assert(!syncTime() && boot().elapsedMs==10000 && boot().starts==0 && boot().registrations==0);
    assert(boot().disconnects==1 && boot().off==1);
    // Previous service is stopped before this attempt owns notifications.
    boot()={};boot().sntpEnabled=true;assert(syncTime());
    assert(boot().order==std::vector<int>({0,1,2,3,0,4}) && !boot().callback);
    delayHook()=nullptr;
}
}
int main(){admission();httpOutcomes();requestParameters();independentTrustFailures();freshTime();
    std::puts("PASS: weather trust admission, authenticated call order, redirect refusal/cleanup, independent calendar and fresh boot notification gate (host stand-ins, no actual TLS/JSON success)");}
