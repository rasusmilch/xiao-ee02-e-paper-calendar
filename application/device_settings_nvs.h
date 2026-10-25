#ifndef EE02_DEVICE_SETTINGS_NVS_H
#define EE02_DEVICE_SETTINGS_NVS_H
#include "device_settings_codec.h"
#include <nvs.h>

namespace DeviceSettings {
// Single-threaded synchronous service, not reentrant. One dedicated namespace/key.
// Arduino initializes NVS before setup(); this adapter never initializes, formats,
// erases a partition or touches another namespace. No API logs field values.
static const char Namespace[]="ee02_settings", Key[]="config";
class Service {
public:
    Service() : loaded_(false) {}
    const Values* internal() const { return loaded_?&active_:nullptr; }
    bool presentation(Presentation& out) const {
        if(!loaded_) return false;
        out=present(active_); return true;
    }
    Result load() {
        size_t n=0; Result r=read(observed_,n);
        if(r.status==Status::Ok) r=decode(observed_,n,active_,work_);
        loaded_=r.status==Status::Ok; return r;
    }
    Result save(const Values& candidate) {
        size_t n=0; Result r=encode(candidate,encoded_,sizeof(encoded_),n);
        if(r.status!=Status::Ok) return r; // Admission before any NVS access.
        size_t old=0; r=read(observed_,old);
        if(r.status!=Status::Ok && r.status!=Status::Missing && r.status!=Status::Corrupt) return r;
        if(r.status==Status::Ok && old==n && !memcmp(encoded_,observed_,n)) {
            active_=candidate; loaded_=true; return result(Status::Unchanged);
        }
        nvs_handle_t h=0; esp_err_t e=nvs_open(Namespace,NVS_READWRITE,&h);
        if(e!=ESP_OK) return storage(e,Status::OpenFailure);
        e=nvs_set_blob(h,Key,encoded_,n);
        if(e!=ESP_OK) { nvs_close(h); return storage(e,Status::WriteFailure); }
        e=nvs_commit(h); nvs_close(h);
        if(e!=ESP_OK) return storage(e,Status::CommitFailure);
        size_t verified=0; r=read(observed_,verified);
        if(r.status!=Status::Ok || verified!=n || memcmp(encoded_,observed_,n))
            return result(Status::VerifyFailure,Field::None,r.storageError);
        active_=candidate; loaded_=true; return result(Status::Ok);
    }
    // Public edit values contain no saved secrets. No secret-presence boolean is
    // interpreted as a replacement. Caller must choose Keep/Replace/Clear.
    Result saveEdit(const Presentation& p,const SecretEdit& password,const SecretEdit& key) {
        Result r=candidate(p,internal(),password,key,work_);
        return r.status==Status::Ok?save(work_):r;
    }
    Result clear() {
        nvs_handle_t h=0; esp_err_t e=nvs_open(Namespace,NVS_READWRITE,&h);
        if(e!=ESP_OK) return storage(e,Status::OpenFailure);
        e=nvs_erase_key(h,Key);
        if(e!=ESP_OK && e!=ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return storage(e,Status::ClearFailure); }
        // Missing key needs no commit; an explicit first clear may create only our namespace.
        if(e==ESP_OK) e=nvs_commit(h); else e=ESP_OK;
        nvs_close(h);
        if(e!=ESP_OK) return storage(e,Status::CommitFailure);
        size_t n=0; const Result r=read(observed_,n);
        if(r.status!=Status::Missing) return result(Status::VerifyFailure,Field::None,r.storageError);
        active_={}; loaded_=false; return result(Status::Ok);
    }
private:
    Values active_={}, work_={}; bool loaded_;
    // Fixed service-local workspace, outside acquisition PSRAM and task stack.
    uint8_t encoded_[MaxEncoded]={}, observed_[MaxEncoded]={};
    static Result storage(esp_err_t e,Status fallback) {
        if(e==ESP_ERR_NVS_NOT_INITIALIZED) return result(Status::NotInitialized,Field::None,e);
        if(e==ESP_ERR_NVS_NOT_ENOUGH_SPACE || e==ESP_ERR_NVS_VALUE_TOO_LONG)
            return result(Status::InsufficientStorage,Field::None,e);
        if(e==ESP_ERR_NVS_TYPE_MISMATCH) return result(Status::Corrupt,Field::None,e);
        return result(fallback,Field::None,e);
    }
    static Result read(uint8_t* out,size_t& n) {
        n=0; nvs_handle_t h=0; esp_err_t e=nvs_open(Namespace,NVS_READONLY,&h);
        if(e==ESP_ERR_NVS_NOT_FOUND) return result(Status::Missing);
        if(e!=ESP_OK) return storage(e,Status::OpenFailure);
        e=nvs_get_blob(h,Key,nullptr,&n);
        if(e==ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return result(Status::Missing); }
        if(e!=ESP_OK) { nvs_close(h); return storage(e,Status::ReadFailure); }
        if(n<16 || n>MaxEncoded) { nvs_close(h); return result(Status::Corrupt); }
        const size_t expected=n; e=nvs_get_blob(h,Key,out,&n); nvs_close(h);
        if(e!=ESP_OK) return storage(e,Status::ReadFailure);
        return result(n==expected?Status::Ok:Status::ReadFailure);
    }
};
} // namespace DeviceSettings
#endif
