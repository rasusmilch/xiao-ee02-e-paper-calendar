#ifndef EE02_PORTAL_SYSTEM_H
#define EE02_PORTAL_SYSTEM_H
namespace RestartTrace {struct Handoff{};inline unsigned& count(){static unsigned n=0;return n;} }
[[noreturn]] inline void esp_restart(){++RestartTrace::count();throw RestartTrace::Handoff();}
#endif
