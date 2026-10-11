# Engineering reference

This document owns technical constraints, architecture, hardware/security
boundaries, validation strategy, known limitations, stabilization priorities, and
material Open decisions. [README.md](../README.md) owns project intent and operator
setup; [AGENTS.md](../AGENTS.md) owns development workflow and command status.
[CHANGELOG.md](../CHANGELOG.md) is historical project-change evidence.

## Authority and evidence classes

- **Approved:** explicitly selected by the owner; governs desired behavior/work.
- **Current-source observation:** established by inspection; describes implementation,
  not approval of its product policy or proof of runtime success.
- **Recommendation:** a technical proposal not yet selected for execution.
- **Open:** a consequential product/technical choice remains unresolved.
- **Unverified:** a build, service, hardware, or runtime claim lacks adequate evidence.

The current-state sections below are maintained in place. The final two sections
are explicitly historical provenance/evidence; they do not replace current authority.
Passing compilation or host tests cannot establish hardware operation.

## Approved technical constraints

Retain XIAO ESP32-S3 Plus, EE02, and Arduino C++ for stabilization. Preserve calendar, weather, forecast and battery capability in the redesign; retire artwork and its exclusive dependencies. Use static checks and
applicable lightweight host behavior tests during incremental development.
Complete firmware compilation and hardware validation are owner-controlled
milestone activities. A successful firmware build remains preferred but is not
a prerequisite for initial source repairs; uncompiled changes do not establish
firmware compatibility. No CI or hosted build infrastructure is required.
Prioritize correctness, security, reliability, and testability before power
optimization or expansion.
Use a small separation of hardware-independent application logic from drivers
when needed for meaningful tests. A large abstraction framework requires a
demonstrated necessity case against the simpler alternative.

Library upgrades or framework migrations are not assumed prerequisites. Resolve
compatibility through evidence. The five-day UI redesign and device-side multi-calendar
support are approved target scope, not implemented behavior. Artwork retirement is
implemented; the empty former artwork region is only an intermediate development
state, not a completed display design or independently releasable product outcome.
Framework migration remains unselected; legacy defaults establish no target policy.

## Approved target architecture (not yet implemented)

The target below is approved product direction, distinct from the existing firmware
described in the next sections. A roadmap is not implementation authorization.

### Display and event model

- A rolling five-day window starts today, with a fixed but configurable waking-hours
  time axis. A timed five-column grid is provisionally preferred, but compare it with
  an agenda using the actual display before approval. Replace the original 1024x1024
  artwork region with this week view; initially retain the current weather, forecast,
  today's detailed events and battery regions. Retaining the old monthly grid and
  the final dimensions remain Open.
- Multiple selectable calendar sources (Google, iCloud/CalDAV and generic ICS)
  feed a single source-independent event model. No mandatory hosted service,
  external preprocessor or Apps Script backend. ICS-only initial transport is
  **favored, not yet finally selected**; private iCloud requires a different
  privacy/authentication tradeoff from public ICS.
- Ask providers for expanded event occurrences where supported; otherwise implement
  bounded on-device ICS recurrence expansion. Handle rules, exceptions, moved and
  deleted occurrences, all-day/multiday intervals, timezones, DST and cross-window
  series. Explicitly report unsupported features and overflow; never silently
  omit an event and claim a complete schedule. Exact limits, parser strategy and
  transport support require host tests and engineering validation.
- Valid zero events are success. A failed calendar must not suppress successfully
  retrieved calendars: show their events and a clearly named error bar saying the
  overall schedule is incomplete. Treat each source independently.
- Current weather and forecast are independent failure domains: place current
  weather errors inside the weather region; keep previously valid forecast when
  it is still fresh. Any retained forecast needs a verified RTC memory/freshness
  strategy, not NVS churn or an assumption that PSRAM survives deep sleep.
- Three **consecutive complete acquisition-cycle failures** trigger the accepted
  full recovery/error screen. Partial failures use source-specific error indications,
  not blanket suppression. Precisely define counter-reset and mixed-result cases
  in state-machine tests.

### Memory, time and storage

- Battery powered; one user-configurable coordinated fetch/validate/render/display
  cycle covers all current calendar and weather sources. Sync NTP if clock invalid
  or on initial cold boot, otherwise roughly once daily; internal RTC timekeeping
  bridges deep sleep. Never use guessed dates on time loss.
- NVS stores changed durable user configuration only: Wi-Fi, timezone, calendar
  URLs/selection, weather preferences, refresh settings. Do not write weather/events,
  daily dates, update counters, transient fetch state or routine timestamps to NVS.
  RTC-retained memory may hold versioned, validated compact sleep context; rebuild
  or enter explicit error state on reset/retention loss. PSRAM is not deep-sleep
  retained.
- Strict measured stack/internal heap/PSRAM limits. The board nominally has
  8 MB PSRAM. Prefer reusing a large PSRAM workspace for streaming ICS parsing and
  bounded expansion, then transfer a compact **full five-day** event snapshot out
  before reusing the workspace for rendering. Do not alias driver-owned active
  framebuffer data. Direct PSRAM DMA/display transfer is unverified; test exact
  Seeed_GFX/ESP32 core or stage transfers through a small internal DMA buffer.
  Measure memory peaks, fragmentation and largest free block across network,
  parsing and rendering phases.
- Artwork-only functions, OpenAI credential requirements and API calls, PNG/base64
  processing, dithering, cache and image-only dependencies have been removed.
  SPIFFS has no remaining application consumer and is no longer initialized.
  Keep artwork retired permanently; protect shared components from broad cleanup.

### Setup and failure recovery

- Device-hosted settings webpage for Wi-Fi, timezone, calendar URLs/selection,
  weather and refresh preferences. Accessible during explicit awake configuration
  over connected LAN or a physically triggered recovery AP; no web service in
  deep sleep. Unconfigured first boot starts setup; configured cold boot makes
  best-effort Wi-Fi/NTP/data attempts, then shows actionable recovery instructions
  rather than erasing credentials when unsuccessful.
- Recovery AP is **open, with no Wi-Fi password**; no webpage login in this revision.
  Stop service/Wi-Fi and sleep at 10 minutes without meaningful operator activity
  or an absolute 30-minute session duration. Automatic polling, association and
  captive-portal probes must not extend the idle timer. Complete/rollback settings
  saves safely before shutdown.
- Show new passwords during entry as appropriate; after saving, mask input and do
  not return the saved value to browsers. Always display configured calendar URL
  fields in full, even when they contain sensitive secret ICS links. This knowingly
  exposes such URLs during an open AP session; keep them out of logs/reviews.
  Outbound TLS certificate trust remains a separate requirement from web login.
- Use validated/versioned configuration saves; incorrect network credentials must
  not trap the device. Physical factory reset must clear user configuration, not
  silently format unrelated filesystems. Simultaneous Setup + Refresh held five
  seconds is a **proposal**, pending physical button mapping and sleep-wake tests.
  Simple AP entry must not erase settings.

### Staged plan (not execution permission)

1. **Stabilization:** retain valid acquisition/parser integrity repairs and the focused
   artwork removal; verify build, driver, memory, buttons and panel.
2. **MVP:** config/NVS and RTC lifecycle, bounded source adapters and recurrence,
   partial-update states, five-day display, unified battery-conscious refresh.
3. **Pilot:** validate exact firmware on EE02 hardware, network failures, calendar
   exception/DST cases, peak stack/heap/PSRAM, framebuffer transfer, current and
   battery runtime, deep sleep and physical recovery; evidence before readiness.
4. **Expansion:** direct Google/CalDAV private authentication, further UI or security
   only when separately approved.

## Current architecture and responsibilities

**Current-source observations:** a single Arduino sketch includes headers that mix
data acquisition, mutable state, application logic, and rendering. These are source
responsibilities, not isolated layers. Acquisition sequencing is separated into
one hardware-independent helper; the remaining application is not a testable core.

| Component | Responsibility and dependencies |
| --- | --- |
| [Sketch](../xiao-ee02-e-paper-calendar.ino) | Global EPaper instance; boot orchestration, dividers and retained panel drawing/update, display sleep and ESP timer deep sleep. No filesystem initialization; `loop()` is empty. |
| [calendar.h](../calendar.h) | Wi-Fi/NTP time synchronization, calendar date math/grid rendering, HTTPS Apps Script retrieval, volatile event array, subset sorting and event drawing. |
| [weather.h](../weather.h) | Current weather, short forecast and grouped daily forecast retrieval; volatile state; icon/forecast rendering; network orchestration that also calls calendar retrieval. |
| [data_acquisition.h](../data_acquisition.h) | Synchronous, independent fetch sequencing after Wi-Fi connection; reports four individual bool results, disconnects, and returns current-weather success. Uses callbacks without Arduino dependencies or retained state. |
| [battery.h](../battery.h) | ADC enable/sample, assumed voltage conversion, discharge-curve interpolation and battery drawing. |
| [constants.h](../constants.h) | Location/timezone/NTP, retained panel/divider layout, event limit and sleep interval. These defaults are implementation observations. |
| [driver.h](../driver.h) | Declares board-screen 510/T133A01 and EE02 selection; propagation through the intended display library/build is unresolved. |
| [google-calendar-api.gs](../google-calendar-api.gs) | Separate Google Apps Script deployment; query-key authentication and today's events from CalendarApp calendars. |

### Boot to sleep flow

1. Start Serial. No filesystem mount, formatting or artwork initialization occurs.
2. `syncTime()` opens Wi-Fi, uses `configTzTime(TIMEZONE, NTP_SERVER)`, tries local
   time acquisition, and disconnects/turns Wi-Fi off. It returns no synchronization
   status (calendar.h lines 41–57).
3. `updateWeather()` opens Wi-Fi again with the existing bounded connection wait.
   On connection success, `acquireDataSources()` calls current weather, short
   forecast, daily forecast and calendar fetch functions once each, in that order,
   regardless of individual bool results. Each fetch owns its own inputs; forecast
   and calendar requests do not require current-weather success. Four outcomes
   are reported with source-only failure messages, then Wi-Fi disconnects/off.
   The return value still reports current-weather success only. Wi-Fi failure
   returns false without fetching or running disconnect/off cleanup. The calendar
   fetch's existing internal HTTP retry is unchanged.
4. Begin/rotate/clear the display, draw existing dividers, daily forecast, monthly calendar,
   events, battery and weather, then unconditionally call `update()` and `sleep()`.
   No displayed-content comparison exists. The cleared former artwork region is
   reserved for the future five-day calendar; no replacement renderer or placeholder
   is implemented (sketch lines 65–87).
5. Calculate sleep from local time and `DEEP_SLEEP_HOURS`, avoiding 00:00–04:59;
   failed time acquisition falls back to the interval. Enable timer wake and enter
   deep sleep if nonzero (sketch lines 25–57,89–99). Actual wake behavior is unverified.

Retained panel coordinates and divider endpoints are unchanged. `DAILY_FCST_X=10`
replaces the forecast's former artwork-named origin. Neutral divider constants keep
the margin at 10, vertical divider at x=1044 and left horizontal divider at y=1044.
Its endpoints remain x=10..1034. The right horizontal divider retains x=1054 to
display width minus 20 and y=display height / 2 - 30. These preserve current geometry,
not the unselected final week-view layout.

### Service, data and storage boundaries

NTP uses the configured pool server and POSIX timezone. OpenWeatherMap supplies
current weather plus overlapping `cnt=5`/`cnt=40` forecast responses. Forecast
hours/dates are taken from `dt_txt` without explicit timezone normalization
(weather.h lines 191–205,631–659). Apps Script uses deployment-local date/time
operations; live deployment/calendar zone alignment is unverified.

Apps Script truncates while enumerating calendars at eight events; firmware
truncates at six before sorting by HH:MM (script lines 18,38–65; calendar.h
lines 181–207). All-day output is `00:00`/`24:00`. Selection order, filtering and
all-day priority remain Open. Successful JSON parsing alone does not distinguish
an unauthorized/error response from a valid empty event collection. Events and
weather live in ordinary static RAM; no retained calendar snapshot is implemented.

No OpenAI requests, artwork processing/rendering, application filesystem operations
or persistent data cache remain. The removed artwork path owned the overlapping
4MiB+1 response and variable-size decoded PNG PSRAM allocations, prompt strings,
decoder/file state, static dither/error storage and row buffers. This establishes
removed allocation sites, not measured memory savings or verified headroom. No new
workspace, persistent state or framebuffer ownership mechanism is introduced;
display-driver/core allocations and actual stack/heap/PSRAM peaks remain unverified.
Existing flash contents and the documented partition selection are untouched.

Weather failure no longer suppresses calendar or forecast acquisition on a
connected session. Missing time still affects date/render paths; current-weather
failure still causes `drawWeather()` to return before its nested short-forecast draw.
The daily forecast, calendar, events and battery retain their separate sketch calls.
These are observed limitations, not approved degraded-operation policy.

## Hardware platform and known configuration

The target is XIAO ESP32-S3 Plus (Xtensa ESP32-S3), Seeed EE02, and a 13.3-inch
six-color Spectra 6 T133A01 panel with intended 1600x1200 layout. Documented Arduino
inputs are XIAO_ESP32S3_PLUS, USB CDC enabled, OPI PSRAM, Default with spiffs
(3MB APP/1.5MB SPIFFS), and ESP32 core 4.0.0-alpha1 (sketch lines 1–6).
The partition setting is unchanged even though the application no longer uses
SPIFFS. No erasure, formatting, repartitioning or migration accompanies retirement.
Exact FQBN/menu keys, physical flash/PSRAM capacity, partition CSV, actual linker
memory fit and driver compatibility have not been established.

Source names battery ADC input A0 and measurement enable A5, comments these as
GPIO1/GPIO6, pulls enable high, waits 5ms, and assumes a 2:1 divider with linear
3.3V/4095 scaling (battery.h lines 9–10,48–63). Those are unverified board/ADC
assumptions; drawing samples again for logging (line 114).
Display power/sleep is delegated to EPaper. No verified EE02 power-enable pin,
polarity, sequencing or sleep retention contract is established. Do not infer that
`epaper.sleep()` removes panel supply or guarantees low system current.

## Security and credential boundaries

**Current-source observations:** Wi-Fi/API secrets come from intentionally ignored
local credentials.h. Apps Script contains a placeholder API_KEY that operators
must privately configure. HTTPS paths call `setInsecure()` in calendar.h:146,
weather.h:71/151/552. Thus certificate-based server authentication is disabled;
encrypted transport is not authenticated trust.

Calendar/API keys travel in query URLs. Apps Script deployment instructions
execute as the deploying account with access set to
Anyone, guarded in code by the query key. Private calendar information, endpoint
information and API credentials may be exposed through service traffic or Serial
diagnostics. No OpenAI credential is required by the application. Private local
credentials are not rewritten by retirement; existing flash contents are not erased.
Physical-access protection/flash encryption is not established.

**Development constraints:** keep live secrets and sensitive logs out of Git and
shared evidence; do not inspect secret inventories or manufacture credentials to
force validation. Live service calls, deployment, flashing, or destructive recovery
require specific authorization. Operator setup is in README.

**Recommendation, not yet implemented:** authenticated TLS for every endpoint and
redirect, explicit clock/trust failure handling, bounded input validation and
minimal diagnostic data. Trust-update mechanism, physical-access threat assumptions
and recovery/retention policy require evidence or selection.

## Testing and validation strategy

The [AGENTS command section](../AGENTS.md#commands-and-validation) owns actual
command availability. The [focused host regression](../tests/data_acquisition_test.cpp)
calls the production acquisition helper with fake fetch/report/disconnect callbacks.
It covers all 16 success/failure combinations, exactly-once invocation and order,
individual outcomes, current-weather-only return semantics, and the Wi-Fi-failure
gate without any network callbacks. Arduino's connection loop and adapters are
inspected statically; host tests do not exercise their hardware behavior.
No general runner, CI, reproducible firmware compile command or documentation
checker is committed. A full firmware build is unverified. Select checks by
actual affected guarantees; ordinary prose work uses semantic/link/ignore/diff/scope
checks. Static and host checks support incremental source repairs; complete firmware
compilation and hardware validation belong to owner-controlled milestones.

Artwork-retirement checks compare retained source, panel/divider geometry and
boot-to-sleep sequencing against the preceding source revision, and search for dead
artwork symbols, includes, credentials and filesystem consumers. The acquisition
regression does not exercise Arduino rendering, panel behavior or memory peaks.

**Recommended further validation:** recover and pin compatible original inputs
and verify an Arduino CLI build path at an owner-selected milestone. CI/hosted
builds are optional, not required. Extend small host tests of actual extracted
application logic without live APIs. Useful boundaries include date/event selection, explicit source
outcomes/snapshot replacement, timezone/forecast grouping, supplied-time sleep
math, voltage interpolation, render-model comparison and bounded ICS recurrence handling. Keep network, ADC, panel and ESP sleep calls in narrow
hardware adapters. This is intended direction, not the current structure or an
authorization to introduce infrastructure now.

Host tests can establish deterministic logic; compilation establishes build
compatibility/size only. Service integration needs authorized endpoint/deployment
checks. Hardware verification remains required for:

- EE02 enable/panel supply/SPI states: inspect matching schematics/driver and scope
  boot, refresh, sleep and wake sequencing.
- Battery conversion: compare a DMM/reference across the operating range; verify
  calibration, attenuation, divider, settling and enable leakage.
- Sleep current: instrument the actual board/panel, including failure paths and
  documented power/USB conditions; acceptance limits remain Open.
- Display: verify colors, layout, busy/timeouts, initialization refresh, repeated
  refresh and panel sleep/wake with the selected driver.
- Wake/Wi-Fi: timestamped interval/DST/missing-time tests; unavailable AP, drop,
  DNS/NTP/TLS failure and recovery across repeated cycles.
- PSRAM/runtime: observe allocation headroom, stack peaks, fragmentation,
  largest free blocks and resets/watchdog on the exact build/hardware. Future
  shared parser/framebuffer phase lifetimes need separate validation.

Record actual revision/configuration, covered scope, command/method, outcome and
limitations in historical evidence. Distinguish current execution from earlier
evidence carried forward with an explicit applicability judgment. No hardware
measurement or successful firmware build is available to support readiness.

## Known engineering limitations

- **Seeed_GFX/EE02 compatibility unresolved:** sketch references master with version
  2.0.3, not an immutable revision. The inspected V2.0.3 source selects an ILI9341
  default and lacks EE02/510 configuration. driver.h is not explicitly included
  by root application source; calendar.h repeats selection macros after the
  sketch's first TFT_eSPI include. Correct library-wide configuration propagation
  must be established, not assumed or patched by an unapproved upgrade.
- **Alpha core reproducibility unverified:** cached package metadata includes
  4.0.0-alpha1, but availability, complete installation, exact board recipes and
  a successful build are unverified. No installed ESP32 core/toolchain was found.
- **No complete build/regression baseline:** documented library versions are not
  compatible revision proof; credentials/configuration, memory and partition fit
  remain prerequisites for a verified complete build. Host coverage is limited
  to acquisition orchestration; no CI is committed.
- **Data reliability:** truncation before global sort,
  insufficient response-state distinctions, volatile events and unresolved timezone
  alignment limit confidence. Retention behavior is not yet selected.
- **Security:** insecure TLS remains and requires stabilization; removing artwork
  does not establish authenticated trust for weather or calendar traffic.
- **Intermediate display:** the reserved former artwork region is temporarily empty.
  Artwork removal is not a complete or independently releasable display design;
  the future week view and measured memory/display behavior remain unverified.
- **Power/hardware:** repeated network operations, unconditional display update,
  unmeasured sleep current/power sequencing, and unverified battery/display behavior
  remain. Static review cannot determine physical refresh mode or electrical accuracy.

## Delivery roadmap and stabilization order

The approved stages and exact scope are owned by the [target architecture
section](#staged-plan-not-execution-permission). This roadmap does not authorize
implementation, merge, dependency upgrades or hardware flashing. Finish
unmerged correctness work under its own validated scope, then review independently
the hardware/memory baseline, calendar transport and week-view display before
implementation. Review the completed artwork removal independently. Each semantic
unit requires separate approval, applicable tests and documentation reconciliation.
Full firmware compile and
hardware validation are owner-controlled milestones, not a prerequisite for
initial source repairs.

## Open decisions and recommendation boundaries

**Remaining Open policy:** final agenda/grid choice and dimensions, retention of
legacy month grid, waking-hour defaults, recurrence and overflow limits, ICS-only
transport commitment versus private CalDAV/Google authenticated access, weather
freshness and retained-buffer fit, failure state transitions, actual button reset
mapping, driver PSRAM/DMA compatibility, refresh default/battery-life targets,
and hardware pilot acceptance. Current constants do not decide these.

**Recommendations not yet selected:** Arduino CLI as the reproducible build route,
immutable compatible library pins, broader focused C++ host coverage, explicit source
results/time normalization, transactional storage and authenticated TLS mechanisms.
Routine helper organization/test tooling is an implementation-local choice; an
upgrade, durable retention guarantee or product-policy change needs supporting
evidence and approval. Unknown wiring/configuration/runtime facts require inspection
or measurement, not an owner vote on guessed values.

## Historical approved-decision provenance

**Historical evidence; current constraints are stated above and intent in README.**
In the 2026-10-10 supplied governance execution request, the owner explicitly
confirmed existing-project adoption, hardware/Arduino retention, advertised
capability protection, correctness/security/reliability/testability priority,
baseline-before-repair sequencing, small testable separation, and deferral of UI/
provider/framework expansion. The owner authorized only the lean governance unit
and local durable preservation; later roadmap stages and Open policy were not
authorized. Codex recording this direction is not its provenance.

On 2026-10-11, the owner explicitly superseded the earlier requirement that a
reproducible firmware build precede substantive repairs. The revised policy permits
initial source repairs validated with static and applicable lightweight host checks;
the owner controls complete firmware compilation/flashing and hardware milestones.
Compilation remains preferred, uncompiled changes carry no firmware-compatibility
claim, and CI/hosted build infrastructure is not required. Hardware/Arduino retention
and the other stabilization constraints continue to apply. This instruction also
authorized independent weather/forecast/calendar acquisition and its focused tests.

The accepted planning assessment is prior execution evidence, not independent
product authority. No historical firmware releases or earlier owner-policy decisions
are inferred from commit messages. Future material changes preserve truthful
supersession here and reconcile their current-state owner separately.

On 2026-10-11, the owner approved the rolling five-day replacement for artwork,
removal of artwork-only code, no external calendar preprocessor, on-device multiple
calendar normalization, configuration-only NVS, RTC transient context, careful
PSRAM workspace reuse, coordinated refresh with approximately daily NTP, open
temporary AP (10-minute inactivity/30-minute absolute session), a device-hosted
configuration webpage, and partial source-specific calendar/weather failure
presentation. Three successive complete acquisition failures invoke an error
screen. These supersede earlier artwork protection and deferred UI/provider
direction **as target design**, not as a claim of source implementation. ICS-only
initial transport and grid layout remain favored proposals. The two-button
factory reset gesture and direct PSRAM transfer are unverified. No firmware,
service or hardware validation is implied by this governance update.

## Historical validation evidence and hardware status

**Evidence history, not a build/hardware success claim.** The accepted read-only
assessment inspected firmware revision
`faa25325e75429e7a6a9d10344869a481fd515c6`, found a clean tree and no committed
tests/build automation, and passed Git whitespace and Apps Script syntax checks.
That syntax result is historical, was not rerun for this prose change, and proves
neither Apps Script deployment behavior nor firmware compilation.

The governance execution rechecked the same clean source baseline, inspected the
supplied guide directly from the ZIP, and verified source-backed documentation
discrepancies. The supplied guide describes v4.1 but contains no Git revision
metadata; ZIP SHA256 is
`c2b78fe1260f192fcbc624afdaad4d82204e45ace517227bd4ee978801870a2e`.
Its six requested workflow documents and exact templates/AGENTS.md and
templates/CHANGELOG.md were reconciliation inputs. Architecture, hardware,
security and testing templates informed only applicable consolidated sections;
no distributed template/guide file is project authority or required at runtime.

Prepared tooling observations: Python is available; pytest executable/module,
PlatformIO and Arduino CLI on PATH are absent. Arduino CLI exists outside the
repository in the prepared environment; earlier assessment verified version 1.3.1
with its explicit configuration. The inspected ESP32 packages directory remains
empty. These are environment-scoped observations, not portable installation claims.

Governance-only checks on 2026-10-10: local Markdown validation resolved 23 file/
fragment links and checked fences/whitespace; byte comparison found all 81 protected
baseline files unchanged; scope inspection found exactly the five authorized files;
ignore comparison showed only the blanket docs rule removed; one canonical changelog
was present. These checks establish documentation/scope evidence, not firmware behavior.

Firmware compilation and live-service/hardware validation remain NOT RUN. EE02
power sequencing/sleep current, display/driver operation, ADC accuracy, SPIFFS
resilience, wake timing and Wi-Fi recovery are UNVERIFIED; no measured electrical
parameters or production readiness are claimed.
