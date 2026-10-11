# Engineering reference

This document owns technical constraints, architecture, hardware/security
boundaries, validation strategy, known limitations, stabilization priorities, and
material Open decisions. [README.md](../README.md) owns project intent and operator
setup; [AGENTS.md](../AGENTS.md) owns development workflow and command status.
[CHANGELOG.md](../CHANGELOG.md) is historical project-change evidence.

## Authority and evidence classes

- **Approved:** explicitly selected by the owner; governs desired behavior/work.
- **Selected:** an owner-selected architectural direction, not proof of current
  implementation, dependency compatibility or validated operating limits.
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

Seeed_GFX2 with a local external-framebuffer ownership extension is Selected for
the target; the current Seeed_GFX/TFT_eSPI integration has not migrated. Other
library upgrades or framework migrations are not assumed prerequisites. Resolve
compatibility through evidence. The five-day UI redesign and device-side multi-calendar
support are approved target scope, not implemented behavior. Artwork retirement is
implemented; the empty former artwork region is only an intermediate development
state, not a completed display design or independently releasable product outcome.
Legacy defaults establish no target policy; exact GFX2/core/board integration
remains unverified and this selection alone does not authorize implementation.

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
  omit an event and claim a complete schedule. Sequential streaming ICS with bounded
  dependency staging is Selected below; exact limits, supported recurrence subset
  and provider transport support require separate adjudication and validation.
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
- Strict measured stack/internal heap/PSRAM limits. The selected shared workspace
  and independent **full five-day** snapshot follow the ownership and validation
  contract below; nominal PSRAM capacity does not establish usable headroom.
- Artwork-only functions, OpenAI credential requirements and API calls, PNG/base64
  processing, dithering, cache and image-only dependencies have been removed.
  SPIFFS has no remaining application consumer and is no longer initialized.
  Keep artwork retired permanently; protect shared components from broad cleanup.

### Selected GFX2, PSRAM and streaming-calendar architecture

**Selected target, not implemented or validated.** The owner-selected architecture
uses Seeed_GFX2 with a locally maintained external-framebuffer ownership extension,
one application-owned PSRAM arena, and a separate fixed-capacity five-day occurrence
snapshot in PSRAM. Both allocations are volatile across deep sleep. Current firmware
still uses the global TFT_eSPI/EPaper object and ordinary event/weather storage;
it implements none of this ownership extension, arena or streaming ICS pipeline.
Contradictory engineering evidence must trigger explicit review and adjudication,
not silent architectural substitution. Detailed parser limits remain Open.

#### Ownership and wake-cycle sequence

1. Allocate and verify the application-owned workspace and independent snapshot.
   Do not silently fall back to internal SRAM for either large PSRAM allocation.
2. Acquire calendars sequentially, one source at a time, through streaming HTTP/ICS.
   Never materialize a whole feed as a `String` or complete in-memory ICS document.
   Correctly handle response framing/completion, redirects, timeouts and truncation.
3. Use the arena for bounded line unfolding/tokenization, compact components,
   recurrence rules/exceptions, timezone definitions and expansion state. Input
   order is not reliable: masters, overrides and timezone definitions may follow
   their consumers. Resolve dependencies and exceptions before publishing the
   calendar's five-day occurrences, including moves into or out of the window.
4. Validate and publish each calendar transactionally, using a checkpoint/rollback
   or equivalent isolated staging. Complete success, including valid zero events,
   publishes only independent snapshot records. Unsupported rules, malformed or
   truncated responses, timeouts, exhausted budgets or other failures roll back
   only that calendar; name it as incomplete and continue other sources. Never
   expose a failed source's partial occurrences as a complete result.
5. Reset source-local arena state between calendars. Snapshot records must retain
   no pointers, offsets, `String` backing storage, UID pools, recurrence iterators
   or timezone mappings into the reused arena. Snapshot data remains valid for
   the entire wake cycle independently of scratch storage.
6. Close network and parser activity before rendering. Clear the first
   framebuffer-sized prefix of the arena and lend it exclusively to GFX2 for
   calendar, weather, forecast, battery and error composition. No parsing writes
   may overlap this live framebuffer. Complete panel refresh and sleep before
   ending the borrow or releasing/reassigning the application-owned storage.

This is phase reuse of application-owned memory, not reuse of a driver-owned
pointer. Do not add a general buffer manager, compatibility layer, persistent cache
or secondary representation without a demonstrated need against this simpler
sequence. The selection does not change existing layout or authorize a renderer,
calendar parser, configuration system or refresh scheduler implementation.

#### GFX2 external-framebuffer contract and evidence

Use an appropriately licensed, pinned, locally maintained GFX2 adaptation with
the smallest explicit external-framebuffer interface. In borrowed mode, GFX2
borrows only the validated **960,000-byte framebuffer slice**, never the arena
tail. It must not free application-owned memory, resize/replace it, retain it past
the borrow lifetime or allocate an unnecessary second full framebuffer. Borrowed
mode must never silently allocate a second full framebuffer.
Reject invalid capacity, alignment, format and lifecycle states cleanly; preserve
ordinary library-owned behavior for other callers. No unrelated DMA or panel
controller-protocol changes are selected.

The selected native T133A01 format is 1200 × 1600 at 4 bits/pixel: 600 bytes/row
and a calculated 960,000 bytes (937.5 KiB). Rotation to 1600 × 1200 keeps the same
storage size. This is format-based arithmetic, not an observed current allocation.
Audit mirror paths for temporary full-screen copies and verify orientation;
selecting external ownership alone does not prove no secondary allocations occur.

The supplied architecture reference reports research at Seeed_GFX2 revision
`8f74b7aa91c4fcbc5ba2467bab4de4e7ce0eeeeb`, specifically
`src/panel/Panel_EPaper.cpp` and `src/driver/epaper/Driver_T133A01.cpp`, with
CPU-mediated indexed-color conversion and SPI sends. This is supplied research
evidence, not independently inspected driver source in this documentation task
or a validated dependency pin. Direct PSRAM DMA is neither required by the
selection nor established as supported. Verify exact source, core/board
configuration propagation, packaging, transfer/alignment requirements and physical
EE02 operation before claiming compatibility; do not substitute arbitrary versions.

#### Provisional evaluation budgets

| Item | Initial evaluation allowance; not a validated operating limit |
| --- | --- |
| Shared PSRAM arena | **2 MiB (2,097,152 bytes)** for source staging, then the 960,000-byte framebuffer prefix |
| Independent five-day snapshot | **1,024 occurrences × estimated 320 bytes = 327,680 bytes (320 KiB)** |

The occurrence count, snapshot capacity and arena size are provisional evaluation
budgets. The 320-byte record allowance is not an implemented `sizeof` or final
record schema. Illustrative additional allowances in the supplied reference are
64 KiB source metadata, 16 KiB weather and 16 KiB ordering/layout indices; these
are estimates, not selected fixed allocations or measurements. Actual TLS/network,
Arduino/core/task-stack and graphics temporary-buffer costs remain unmeasured.
Revise budgets through measured workloads and explicit review, not silent overflow
or fallback. Nominal 8 MiB PSRAM proves neither a sufficiently large contiguous
block nor adequate internal heap. Measure overlapping lifetimes, minimum internal
heap, largest free PSRAM block, fragmentation, stack margins, processing time and
panel-cycle energy before establishing operating limits.

#### Recurrence correctness, limits and transport boundaries

Streaming bounds input buffering, not dependency state for arbitrarily large valid
feeds. Preserve full UID identities without unsafe truncation. The supported
contract must correctly resolve `RECURRENCE-ID`, `RRULE`, `RDATE`, `EXDATE`,
cancellations/moved instances, timezone/DST, all-day/multiday overlap and out-of-order
definitions. Supported subsets, source-byte/logical-line bounds, component/exception
limits, recurrence-candidate/CPU budgets and event-capacity overflow behavior need
separate adjudication. Enforce explicit limits and name incomplete sources; never
silently omit occurrences while reporting a complete schedule.

Normal processing uses one download with compact staging. A two-pass streaming
fallback is optional only if measurements demonstrate necessity and a strong
validator or equivalent confirms identical source content across both passes.
`304 Not Modified` cannot reconstruct volatile calendar data lost across deep sleep.
Provider transports remain Open: selecting streaming ICS does not commit all sources
to ICS-only access. Prefer bounded provider-expanded results where a future authorized
adapter supports them. No always-on external processor or flash spool/cache is
selected.

#### Required implementation validation

- **Host:** folded lines across arbitrary network chunks, response completion and
  truncation, oversized ignored properties, timezone/DST, masters/overrides in
  either order, window-crossing moves, per-source rollback and allocator exhaustion.
  Poison/reset the arena before rendering to expose dangling snapshot references;
  verify bounded parser/recurrence state and no unbounded per-event heap allocation.
- **Exact firmware/toolchain:** verify GFX2/board/core configuration at first include
  and throughout library compilation, native format/capacity/alignment, initial
  allocation/failure paths, no frees of borrowed memory, borrow lifetime through
  transfer/sleep and absence of unnecessary secondary framebuffer allocations.
- **Physical EE02:** six colors and rotated coordinates; retained weather/forecast,
  calendar/details and battery rendering; repeated initialization/refresh/sleep/wake;
  mirror temporary allocations and failure paths. Measure minimum internal heap,
  PSRAM peaks/largest free blocks/fragmentation, stack high-water marks, TLS overlap,
  processing/refresh time and energy on the exact build. Host tests cannot establish
  DMA support or physical display correctness. No such measurements are claimed here.

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

The selected architecture's [required validation](#required-implementation-validation)
adds ownership, streaming/recurrence, rollback and measured memory/display checks;
it does not describe tests already implemented or executed.

**Recommended further validation:** establish reproducible inputs for the selected
GFX2 integration and verify an Arduino CLI build path at an owner-selected milestone;
any attempt to reproduce the legacy prototype must separately identify its compatible
original inputs. CI/hosted
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
- **Selected GFX2 integration unimplemented/unverified:** the external-framebuffer
  extension, dependency pin, licensing/packaging, exact core/board configuration,
  borrowed-memory lifetime and panel transfer behavior need implementation and
  validation. Supplied upstream research does not establish compatibility or memory
  reliability on EE02. Graphics-foundation selection is no longer Open.
- **Alpha core reproducibility unverified:** cached package metadata includes
  4.0.0-alpha1, but availability, complete installation, exact board recipes and
  a successful build are unverified. No installed ESP32 core/toolchain was found.
- **No complete build/regression baseline:** documented library versions are not
  compatible revision proof; credentials/configuration, memory and partition fit
  remain prerequisites for a verified complete build. Host coverage is limited
  to acquisition orchestration; no CI is committed.
- **Data reliability:** truncation before global sort,
  insufficient response-state distinctions, volatile events and unresolved timezone
  alignment limit confidence. The target calendar snapshot is wake-cycle-only;
  weather freshness/RTC-retention details remain Open.
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
mapping, exact GFX2 dependency/core/board integration and verified PSRAM/transfer
behavior (direct DMA remains unverified, not required), refresh default/battery-life targets,
and hardware pilot acceptance. Current constants do not decide these.

Seeed_GFX2, application-owned phase reuse, the independent PSRAM snapshot,
sequential streaming and per-calendar transactional publication are **Selected**,
not Open alternatives. Numeric evaluation budgets are provisional, not validated
limits. Contrary engineering evidence requires explicit review and adjudication.

**Recommendations not yet selected:** Arduino CLI as the reproducible build route,
immutable compatible pins for other dependencies, broader focused C++ host coverage, explicit
source-results/time normalization, durable configuration-save mechanisms and
authenticated TLS mechanisms. Pinning the selected local GFX2 adaptation is required,
but its exact compatible revision is not yet established.
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

On 2026-10-11, the owner selected one feature branch (`feature/ee02-week-view`),
independently reviewable semantic commits and one eventual draft PR for the complete
redesign. The owner superseded the blanket Codex publication prohibition with
standing permission for controlled non-force publication of validated local commits
to the designated branch. This supersedes exclusive user/ChatGPT ownership of
feature-branch publication, while PR management, integration approval and merging
remain separately authorized. Independent ChatGPT review follows publication before
authorization of the next dependent implementation unit. The current policy is
owned by [AGENTS.md](../AGENTS.md#authorized-feature-branch-publication); this record
is decision provenance, not evidence of review or merge completion.

On 2026-10-11, the owner supplied `ee02_selected_memory_architecture.md` and selected
Seeed_GFX2 with a locally maintained external-framebuffer ownership extension,
application-owned PSRAM phase reuse, a separate fixed-capacity five-day PSRAM
snapshot, sequential streaming ICS and transactional per-calendar publication.
This supersedes unselected graphics-foundation wording and conditional workspace
reuse as target direction; earlier decision and validation evidence remains history.
The 2 MiB arena, 1,024 occurrences and 320 KiB snapshot are provisional evaluation
budgets. The supplied GFX2 research revision is not a validated dependency pin;
no migration, parser implementation, firmware build or memory/display measurements
are implied. Current authority is the [selected architecture](#selected-gfx2-psram-and-streaming-calendar-architecture),
which requires explicit adjudication of contradictory engineering evidence.

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
