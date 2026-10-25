#ifndef EE02_TEST_NVS_H
#define EE02_TEST_NVS_H
#include <stddef.h>
#include <stdint.h>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>
#include <map>
#ifndef EE02_HOST_ESP_ERRORS
#define EE02_HOST_ESP_ERRORS
using esp_err_t=int;
static const esp_err_t ESP_OK=0,ESP_ERR_INVALID_ARG=0x102;
#endif
static const esp_err_t ESP_ERR_NVS_NOT_FOUND=0x1102, ESP_ERR_NVS_NOT_INITIALIZED=0x1101,
 ESP_ERR_NVS_NOT_ENOUGH_SPACE=0x1105, ESP_ERR_NVS_VALUE_TOO_LONG=0x110e,
 ESP_ERR_NVS_TYPE_MISMATCH=0x1103, ESP_ERR_NVS_INVALID_LENGTH=0x110c;
using nvs_handle_t=uint32_t;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
namespace NvsHost {
struct State {
 bool exists=false, key=false; std::vector<uint8_t> durable,pending;
 bool pendingSet=false,pendingErase=false,readonly=true,opened=false;
 int openError=0,readError=0,writeError=0,commitError=0,eraseError=0;
 bool immediate=false, corruptVerify=false;
 size_t opens=0,reads=0,sets=0,commits=0,erases=0,closes=0;
 std::map<std::string,std::string> unrelated={{"other/key","untouched"}};
};
inline State& state() { static State s; return s; }
}
inline esp_err_t nvs_open(const char* name,nvs_open_mode_t mode,nvs_handle_t* h) {
 auto& s=NvsHost::state();assert(!strcmp(name,"ee02_settings") && !s.opened);++s.opens;
 if(s.openError)return s.openError;
 if(!s.exists && mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;
 s.exists=true;s.opened=true;s.readonly=mode==NVS_READONLY;*h=1;return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* p,size_t* n) {
 auto& s=NvsHost::state();assert(s.opened && h==1 && !strcmp(key,"config"));++s.reads;
 if(s.readError)return s.readError;
 if(!s.key)return ESP_ERR_NVS_NOT_FOUND;
 if(!p){*n=s.durable.size();return ESP_OK;}
 if(*n<s.durable.size())return ESP_ERR_NVS_INVALID_LENGTH;
 memcpy(p,s.durable.data(),s.durable.size());*n=s.durable.size();
 if(s.corruptVerify && s.sets && *n)static_cast<uint8_t*>(p)[0]^=1;
 return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* p,size_t n) {
 auto& s=NvsHost::state();assert(s.opened && !s.readonly && h==1 && !strcmp(key,"config"));++s.sets;
 if(s.writeError)return s.writeError;
 s.pending.assign(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);s.pendingSet=true;
 if(s.immediate){s.durable=s.pending;s.key=true;}
 return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t h) {
 auto& s=NvsHost::state();assert(s.opened && !s.readonly && h==1);++s.commits;
 if(s.commitError)return s.commitError;
 if(s.pendingSet){s.durable=s.pending;s.key=true;}
 if(s.pendingErase){s.durable.clear();s.key=false;}
 s.pendingSet=s.pendingErase=false;return ESP_OK;
}
inline esp_err_t nvs_erase_key(nvs_handle_t h,const char* key) {
 auto& s=NvsHost::state();assert(s.opened && !s.readonly && h==1 && !strcmp(key,"config"));++s.erases;
 if(s.eraseError)return s.eraseError;
 if(!s.key)return ESP_ERR_NVS_NOT_FOUND;
 s.pendingErase=true;if(s.immediate){s.durable.clear();s.key=false;}return ESP_OK;
}
inline void nvs_close(nvs_handle_t h) {
 auto& s=NvsHost::state();assert(h==1 && s.opened);++s.closes;s.opened=false;
 s.pending.clear();s.pendingSet=s.pendingErase=false;
}
#endif
