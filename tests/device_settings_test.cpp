#include "../application/device_settings_nvs.h"
#include "device_settings_fixture.h"
#include <cstdio>
#include <vector>
#include <algorithm>
using namespace DeviceSettings;
namespace {
uint8_t bytes[MaxEncoded+32],other[MaxEncoded];Values output={},work={};size_t n=0,cases=0;
void crc(){codec::put(bytes+12,checksum(bytes,n),4);}
void rejected(Status expected=Status::Corrupt){
 Values before=output;assert(decode(bytes,n,output,work).status==expected);
 assert(!memcmp(&before,&output,sizeof(output)));++cases;
}
void noWrite(const NvsHost::State& before) {const auto& s=NvsHost::state();assert(s.sets==before.sets && s.commits==before.commits && s.erases==before.erases && !s.opened);}
}
int main(){
 auto v=SettingsFixture::complete();assert(validate(Values{}).status==Status::InvalidSettings);
 assert(encode(v,bytes,sizeof(bytes),n).status==Status::Ok);const size_t original=n;
 assert(decode(bytes,n,output,work).status==Status::Ok);size_t m=0;assert(encode(output,other,sizeof(other),m).status==Status::Ok && m==n && !memcmp(bytes,other,n));
 memset(bytes,0,n);assert(!strcmp(output.sources[0].label.data,"Personal"));++cases;
 // Complete bounds: eight sources, 512-byte token-bearing URLs, UTF-8 labels.
 auto max=v;max.sourceCount=MaxSources;
 for(size_t i=0;i<MaxSources;++i){max.sources[i]=v.sources[0];std::string url="https://example.invalid/?token=";url+=std::string(512-url.size(),'x');SettingsFixture::set(max.sources[i].url,url.c_str());SettingsFixture::set(max.sources[i].label,std::string(64,'L').c_str());}
 SettingsFixture::set(max.ssid,std::string(32,'S').c_str());SettingsFixture::set(max.password,std::string(64,'a').c_str());
 SettingsFixture::set(max.timezone,std::string(127,'T').c_str());
 std::string host=std::string(63,'a')+"."+std::string(63,'b')+"."+std::string(63,'c')+"."+std::string(61,'d');SettingsFixture::set(max.ntp,host.c_str());
 max.weatherEnabled=false;SettingsFixture::set(max.location,std::string(127,'L').c_str());SettingsFixture::set(max.apiKey,std::string(128,'k').c_str());
 assert(encode(max,bytes,sizeof(bytes),n).status==Status::Ok && n<=MaxEncoded);assert(decode(bytes,n,output,work).status==Status::Ok);++cases;
 // Zero selection and disabled blank URLs survive encoding.
 v.sourceCount=0;v.weatherEnabled=false;assert(encode(v,bytes,sizeof(bytes),n).status==Status::Ok);assert(decode(bytes,n,output,work).status==Status::Ok && !output.sourceCount);v=SettingsFixture::complete();
 for(size_t i=0;i<original;++i){assert(encode(v,bytes,sizeof(bytes),n).status==Status::Ok);n=i;rejected();}
 for(int type=0;type<10;++type){assert(encode(v,bytes,sizeof(bytes),n).status==Status::Ok);
  if(type==0)bytes[0]^=1;
  if(type==1)bytes[12]^=1;
  if(type==2){bytes[4]=2;crc();}
  if(type==3){bytes[25]=9;crc();}
  if(type==4){bytes[26]=255;bytes[27]=255;crc();}
  if(type==5){bytes[28]=0;crc();}
  if(type==6){bytes[28]=0xc0;crc();}
  if(type==7){codec::put(bytes+16,0,8);crc();}
  if(type==8){bytes[n++]=0;codec::put(bytes+8,n,4);crc();}
  if(type==9){codec::put(bytes+8,UINT32_MAX,4);crc();}
  rejected(type==2?Status::UnsupportedVersion:Status::Corrupt);
 }
 for(int type=0;type<10;++type){auto bad=v;
  if(type==0)bad.sourceCount=255;
  if(type==1)bad.ssid.size=33;
  if(type==2)bad.ssid.data[bad.ssid.size]='x';
  if(type==3)bad.sources[0].label.data[0]=char(0xff);
  if(type==4)SettingsFixture::set(bad.sources[0].url,"http://example.invalid/a");
  if(type==5)bad.refreshSeconds=UINT64_MAX;
  if(type==6)SettingsFixture::set(bad.apiKey,"bad&key");
  if(type==7)SettingsFixture::set(bad.location,"\n");
  if(type==8)SettingsFixture::set(bad.password,"short");
  if(type==9)SettingsFixture::set(bad.ntp,"-invalid");
  assert(validate(bad).status==Status::InvalidSettings);NvsHost::state()={};Service s;assert(s.save(bad).status==Status::InvalidSettings && !NvsHost::state().opens);++cases;
 }
 assert(!assign(v.ssid,reinterpret_cast<const char*>(UINTPTR_MAX-1),4));
 assert(!assign(v.ssid,"embedded\0nul",12));
 assert(!assign(v.ssid,std::string(33,'s').data(),33));
 // Storage: missing/open/init/read/type/corruption/future; never writes on loads.
 NvsHost::state()={};Service s;assert(s.load().status==Status::Missing && !s.internal());
 for(int error:{77,ESP_ERR_NVS_NOT_INITIALIZED}){NvsHost::state().openError=error;assert(s.load().status==(error==77?Status::OpenFailure:Status::NotInitialized));}NvsHost::state().openError=0;
 assert(s.save(v).status==Status::Ok);assert(NvsHost::state().sets==1 && NvsHost::state().commits==1);++cases;
 auto baseline=NvsHost::state();assert(s.load().status==Status::Ok);assert(s.save(v).status==Status::Unchanged);noWrite(baseline);
 NvsHost::state().readError=77;assert(s.load().status==Status::ReadFailure);noWrite(baseline);NvsHost::state().readError=0;assert(s.load().status==Status::Ok);
 auto persisted=NvsHost::state().durable;
 for(int type=0;type<3;++type){NvsHost::state().durable=persisted;
  if(type==0)NvsHost::state().durable[12]^=1;
  if(type==1){auto& d=NvsHost::state().durable;d[4]=2;codec::put(d.data()+12,checksum(d.data(),d.size()),4);}
  if(type==2)NvsHost::state().durable.resize(MaxEncoded+1);
  assert(s.load().status==(type==1?Status::UnsupportedVersion:Status::Corrupt));noWrite(baseline);++cases;
 }
 NvsHost::state().durable=persisted;assert(s.load().status==Status::Ok);
 auto changed=v;SettingsFixture::set(changed.ssid,"updated-network");
 for(int type=0;type<5;++type){auto& st=NvsHost::state();st.writeError=st.commitError=0;st.corruptVerify=false;st.immediate=false;st.durable=persisted;
  if(type==0)st.writeError=77;
  if(type==1)st.writeError=ESP_ERR_NVS_NOT_ENOUGH_SPACE;
  if(type==2)st.commitError=77;
  if(type==3){st.commitError=77;st.immediate=true;}if(type==4)st.corruptVerify=true;
  const Status expected[]={Status::WriteFailure,Status::InsufficientStorage,Status::CommitFailure,Status::CommitFailure,Status::VerifyFailure};
  assert(s.save(changed).status==expected[type]);assert(!strcmp(s.internal()->ssid.data,v.ssid.data));assert(!st.opened);++cases;
 }
 NvsHost::state().writeError=NvsHost::state().commitError=0;NvsHost::state().corruptVerify=false;assert(s.load().status==Status::Ok); // failed commit may already have persisted a whole new record
 assert(!strcmp(s.internal()->ssid.data,changed.ssid.data));
 assert(s.save(v).status==Status::Ok);Service reboot;assert(reboot.load().status==Status::Ok && !strcmp(reboot.internal()->ssid.data,v.ssid.data));
 Presentation view={};assert(s.presentation(view));assert(view.hasPassword && view.hasApiKey && !strcmp(view.sources[0].url.data,v.sources[0].url.data));
 static_assert(sizeof(view)<sizeof(Values),"presentation excludes secret storage");
 const auto* viewBytes=reinterpret_cast<const uint8_t*>(&view);
 assert(std::search(viewBytes,viewBytes+sizeof(view),v.password.data,v.password.data+v.password.size)==viewBytes+sizeof(view));
 assert(std::search(viewBytes,viewBytes+sizeof(view),v.apiKey.data,v.apiKey.data+v.apiKey.size)==viewBytes+sizeof(view));
 SecretEdit keep={SecretAction::Keep,nullptr,0}, clear={SecretAction::Clear,nullptr,0};
 baseline=NvsHost::state();assert(s.saveEdit(view,keep,keep).status==Status::Unchanged);noWrite(baseline);
 assert(s.saveEdit(view,{SecretAction::Keep,"********",8},keep).status==Status::InvalidSettings);noWrite(baseline);
 assert(s.saveEdit(view,{SecretAction::Replace,"********",8},keep).status==Status::InvalidSettings);noWrite(baseline);
 assert(s.saveEdit(view,{SecretAction::Replace,"new-password",12},keep).status==Status::Ok);assert(!strcmp(s.internal()->password.data,"new-password"));
 assert(s.saveEdit(view,clear,clear).status==Status::InvalidSettings);assert(s.internal()->password.size); // enabled weather key required
 view.weatherEnabled=false;assert(s.saveEdit(view,clear,clear).status==Status::Ok);assert(!s.internal()->password.size && !s.internal()->apiKey.size);
 assert(s.presentation(view) && !view.hasPassword && !view.hasApiKey);++cases;
 NvsHost::state().eraseError=77;assert(s.clear().status==Status::ClearFailure && s.internal());NvsHost::state().eraseError=0;
 NvsHost::state().commitError=77;assert(s.clear().status==Status::CommitFailure && s.internal());NvsHost::state().commitError=0;
 assert(s.clear().status==Status::Ok && !s.internal());assert(reboot.load().status==Status::Missing);
 assert(NvsHost::state().unrelated.at("other/key")=="untouched" && !NvsHost::state().opened);++cases;
 // Actual failed/mid-write replacement can expose an incomplete record; it
 // must never become a mixed accepted model. Old or full-new recovery is also valid.
 assert(s.save(v).status==Status::Ok);const auto intact=NvsHost::state().durable;
 for(size_t cut=0;cut<intact.size();++cut){NvsHost::state().durable.assign(intact.begin(),intact.begin()+cut);
  assert(reboot.load().status==Status::Corrupt && !reboot.internal());++cases;}
 NvsHost::state().durable=intact;NvsHost::state().readError=ESP_ERR_NVS_TYPE_MISMATCH;
 assert(reboot.load().status==Status::Corrupt);NvsHost::state().readError=0;
 // Complete token-bearing URLs remain visible; edits never expose saved secrets.
 auto link=v;SettingsFixture::set(link.sources[0].url,"https://example.invalid/a?token=synthetic%2Bvalue");
 assert(s.save(link).status==Status::Ok);assert(s.presentation(view));
 assert(!strcmp(view.sources[0].url.data,link.sources[0].url.data));
 assert(!assign(link.apiKey,reinterpret_cast<const char*>(UINTPTR_MAX-1),4));
 assert(s.saveEdit(view,{SecretAction::Replace,reinterpret_cast<const char*>(UINTPTR_MAX-1),8},keep).status==Status::InvalidSettings);
 assert(s.saveEdit(view,{static_cast<SecretAction>(99),nullptr,0},keep).status==Status::InvalidSettings);
 const auto semanticBefore=*s.internal();view.refreshSeconds=0;
 baseline=NvsHost::state();assert(s.saveEdit(view,keep,keep).status==Status::InvalidSettings);noWrite(baseline);
 assert(!memcmp(&semanticBefore,s.internal(),sizeof(Values)));
 assert(encode(v,bytes,sizeof(bytes),n).status==Status::Ok);
 assert(encode(v,bytes,n-1,m).status==Status::InvalidArgument && !m);
 assert(decode(bytes,n,output,output).status==Status::InvalidArgument);
 assert(decode(nullptr,n,output,work).status==Status::InvalidArgument);
 assert(decode(reinterpret_cast<const uint8_t*>(UINTPTR_MAX-1),4,output,work).status==Status::InvalidArgument);
 std::printf("V1 owned values=%zu bytes; service=%zu bytes; maximum encoded=5417/5632 bytes\n",sizeof(Values),sizeof(Service));
 std::printf("Device settings: %zu codec/storage/presentation checks, scripted NVS replacement (not physical power-loss evidence)\n",cases);
}
