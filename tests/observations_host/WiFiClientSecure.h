#ifndef EE02_OBSERVATIONS_TEST_TLS_H
#define EE02_OBSERVATIONS_TEST_TLS_H
namespace ObservationHost { extern unsigned insecureCalls; }
// Existing insecure-path compile/call stand-in only; no TLS or network behavior.
class WiFiClientSecure { public: void setInsecure(){++ObservationHost::insecureCalls;} };
#endif
