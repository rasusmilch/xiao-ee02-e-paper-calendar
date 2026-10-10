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

Retain XIAO ESP32-S3 Plus, EE02, and Arduino C++ for stabilization. Preserve the
advertised calendar, weather, and artwork capabilities. Establish reproducible
development validation before substantive repairs; prioritize correctness,
security, reliability, and testability before power optimization or expansion.
Use a small separation of hardware-independent application logic from drivers
when needed for meaningful tests. A large abstraction framework requires a
demonstrated necessity case against the simpler alternative.

Library upgrades or framework migrations are not assumed prerequisites. Resolve
compatibility through evidence before proposing them. UI redesign, additional
calendar providers, and framework/library migration are deferred. Current defaults
and implementation mechanisms do not establish durable product requirements.

## Current architecture and responsibilities

**Current-source observations:** a single Arduino sketch includes headers that mix
data acquisition, mutable state, application logic, and rendering. These are source
responsibilities, not isolated layers or an already implemented testable core.

| Component | Responsibility and dependencies |
| --- | --- |
| [Sketch](../xiao-ee02-e-paper-calendar.ino) | Global EPaper instance; boot orchestration, SPIFFS mount, drawing/update, display sleep and ESP timer deep sleep. `loop()` is empty. |
| [calendar.h](../calendar.h) | Wi-Fi/NTP time synchronization, calendar date math/grid rendering, HTTPS Apps Script retrieval, volatile event array, subset sorting and event drawing. |
| [weather.h](../weather.h) | Current weather, short forecast and grouped daily forecast retrieval; volatile state; icon/forecast rendering; network orchestration that also calls calendar retrieval. |
| [picture.h](../picture.h) | Weather/season/theme prompt construction, OpenAI chat/image calls, base64/PNG decoding, dithering, date-named SPIFFS cache, cleanup and pixel rendering. Depends on weather state. |
| [battery.h](../battery.h) | ADC enable/sample, assumed voltage conversion, discharge-curve interpolation and battery drawing. |
| [constants.h](../constants.h) | Location/timezone/NTP, themes, layout, event limit and sleep interval. These defaults are implementation observations. |
| [driver.h](../driver.h) | Declares board-screen 510/T133A01 and EE02 selection; propagation through the intended display library/build is unresolved. |
| [google-calendar-api.gs](../google-calendar-api.gs) | Separate Google Apps Script deployment; query-key authentication and today's events from CalendarApp calendars. |

### Boot to sleep flow

1. Start Serial; call `SPIFFS.begin(true)`. Mount failure is logged and execution
   continues; the true argument permits formatting on mount failure
   (sketch lines 61–66).
2. `syncTime()` opens Wi-Fi, uses `configTzTime(TIMEZONE, NTP_SERVER)`, tries local
   time acquisition, and disconnects/turns Wi-Fi off. It returns no synchronization
   status (calendar.h lines 41–57).
3. `updateWeather()` opens Wi-Fi again. Only current-weather success triggers both
   forecast requests and calendar retrieval. Their return values are ignored.
   Normal completion disconnects/off; connection-failure return bypasses that
   cleanup (weather.h lines 890–913).
4. Valid weather gates artwork acquisition. Cache existence can skip generation;
   otherwise another connection obtains a chat prompt and a base64 PNG response,
   decodes/dithers it, writes a raw file and removes old images after reported
   success. Connection failure can also bypass network cleanup
   (picture.h lines 65–406,590–707).
5. Begin/rotate/clear the display, draw artwork, daily forecast, monthly calendar,
   events, battery and weather, then unconditionally call `update()` and `sleep()`.
   No displayed-content comparison exists (sketch lines 78–98).
6. Calculate sleep from local time and `DEEP_SLEEP_HOURS`, avoiding 00:00–04:59;
   failed time acquisition falls back to the interval. Enable timer wake and enter
   deep sleep if nonzero (sketch lines 27–59,100–110). Actual wake behavior is unverified.

### Service, data and storage boundaries

NTP uses the configured pool server and POSIX timezone. OpenWeatherMap supplies
current weather plus overlapping `cnt=5`/`cnt=40` forecast responses. Forecast
hours/dates are taken from `dt_txt` without explicit timezone normalization
(weather.h lines 190–204,630–658). Apps Script uses deployment-local date/time
operations; live deployment/calendar zone alignment is unverified.

Apps Script truncates while enumerating calendars at eight events; firmware
truncates at six before sorting by HH:MM (script lines 18,38–65; calendar.h
lines 181–207). All-day output is `00:00`/`24:00`. Selection order, filtering and
all-day priority remain Open. Successful JSON parsing alone does not distinguish
an unauthorized/error response from a valid empty event collection. Events and
weather live in ordinary static RAM; no retained calendar snapshot is implemented.

OpenAI chat creates a weather/season-aware prompt with a random `PIC_THEMES` entry;
image generation requests a 1024x1024 PNG and processes `b64_json`, not a URL
download. A 4MiB response allocation overlaps the decoded PNG allocation
(picture.h lines 283–397); actual headroom including display/core overhead is unknown.

Artwork cache files are date-named raw 4bpp, expected at 524,288 bytes. Cache-hit
logic checks existence only; rendering checks size and row reads. Writes target
the final filename directly, row write results are unchecked, and no checksum or
transactional publication protects the asset (picture.h lines 497,590–627,
634–647,712–783). PNG header inspection diagnoses dimensions but does not enforce
the fixed-width callback buffer/expected height (lines 512–627).

Failures cross boundaries: weather failure suppresses unrelated calendar/forecast
retrieval and new artwork; missing time affects date/render paths; setup proceeds
without explicit artwork/mount outcomes. These are observed limitations, not
approved degraded-operation policy.

## Hardware platform and known configuration

The target is XIAO ESP32-S3 Plus (Xtensa ESP32-S3), Seeed EE02, and a 13.3-inch
six-color Spectra 6 T133A01 panel with intended 1600x1200 layout. Documented Arduino
inputs are XIAO_ESP32S3_PLUS, USB CDC enabled, OPI PSRAM, Default with spiffs
(3MB APP/1.5MB SPIFFS), and ESP32 core 4.0.0-alpha1 (sketch lines 1–6).
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
weather.h:70/150/551 and picture.h:67/195. Thus certificate-based server
authentication is disabled; encrypted transport is not authenticated trust.

Calendar/API keys travel in query URLs; OpenAI uses a bearer header. Apps Script
deployment instructions execute as the deploying account with access set to
Anyone, guarded in code by the query key. Private calendar information, prompt/
response data, endpoint information, and paid API credentials may be exposed
through service traffic or Serial diagnostics. SPIFFS cache stores artwork;
physical-access protection/flash encryption is not established.

**Development constraints:** keep live secrets and sensitive logs out of Git and
shared evidence; do not inspect secret inventories or manufacture credentials to
force validation. Live service calls, deployment, flashing, or destructive recovery
require specific authorization. Operator setup is in README.

**Recommendation, not yet implemented:** authenticated TLS for every endpoint and
redirect, explicit clock/trust failure handling, bounded input validation, safe cache
publication, and minimal diagnostic data. Trust-update mechanism, physical-access
threat assumptions, and recovery/retention policy require evidence or selection.

## Testing and validation strategy

The [AGENTS command section](../AGENTS.md#commands-and-validation) owns actual
command availability. No automated regression suite, runner, CI, reproducible
compile command, or documentation checker is committed. A full firmware build is
unverified. Select checks by actual affected guarantees, not file extension or
habit; ordinary prose governance work uses semantic/link/ignore/diff/scope checks.

**Recommended future baseline:** pin compatible original inputs and verify one
Arduino CLI build path usable locally and in CI; establish a small host runner
without live APIs. Test actual extracted application logic rather than a substitute
implementation. Useful boundaries include date/event selection, explicit source
outcomes/snapshot replacement, timezone/forecast grouping, supplied-time sleep
math, voltage interpolation, PNG/cache acceptance, dither packing and render-model
comparison. Keep network, filesystem, ADC, panel and ESP sleep calls in narrow
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
- SPIFFS: power cycles/timer wakes, interrupted writes, full storage, corruption,
  and recovery/data-loss behavior.
- Wake/Wi-Fi: timestamped interval/DST/missing-time tests; unavailable AP, drop,
  DNS/NTP/TLS failure and recovery across repeated cycles.
- PSRAM/runtime: observe allocation headroom, decoder behavior, resets/watchdog
  and image-generation resource limits on the exact build/hardware.

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
- **PNGdec fork/buffer compatibility unresolved:** sketch and decoder diagnostics
  name maet3608/PNGdec 1.1.6. Prepared upstream bitbank2/PNGdec 1.1.6 is different
  provenance and defaults to a 2562-byte buffered-row limit, insufficient for a
  1024-pixel RGB/RGBA row. Compatible fork revision/build settings remain unknown.
- **Alpha core reproducibility unverified:** cached package metadata includes
  4.0.0-alpha1, but availability, complete installation, exact board recipes and
  a successful build are unverified. No installed ESP32 core/toolchain was found.
- **No complete build/regression baseline:** documented library versions are not
  compatible revision proof; credentials/configuration, memory and partition fit
  remain prerequisites. No automated tests/CI are committed.
- **Data reliability:** weather/calendar coupling, truncation before global sort,
  insufficient response-state distinctions, volatile events and unresolved timezone
  alignment limit confidence. Retention behavior is not yet selected.
- **Security/storage:** insecure TLS, existence-only cache hits, unchecked direct
  writes, insufficient decoder dimension guards, and format-on-mount-failure
  require stabilization. None is corrected by governance documentation.
- **Power/hardware:** repeated network operations, unconditional display update,
  unmeasured sleep current/power sequencing, and unverified battery/display behavior
  remain. Static review cannot determine physical refresh mode or electrical accuracy.

## Stabilization roadmap

This broad sequence governs planning priorities; it does not authorize execution.
Each later change must have its own focused semantic task, acceptance criteria,
source/authority inspection and independent review. Conditional persistence work
depends on selected retention policy and safe storage publication.

1. **Governance foundation:** coherent repository authority; independent ChatGPT
   review determines governance readiness before progression.
2. **Reproducible build and test baseline:** recover compatible source provenance,
   verify exact board/configuration and build, then add initial meaningful host tests.
3. **Calendar correctness and reliability:** separate source outcomes, correct
   global selection, establish time boundaries and explicit snapshot states;
   implement retention only within a selected policy.
4. **Security and storage integrity:** authenticated TLS, bounded decoding,
   validated transactional cache/recovery; review distinct outcomes separately.
5. **Power and refresh optimization:** shared forecast acquisition, reliable Wi-Fi
   cleanup/lifecycle, selected refresh rules, calibrated battery and verified GPIO/
   display power behavior; measurements determine outcomes.
6. **Integrated validation and hardware pilot:** test the accumulated exact build
   under normal/failure conditions against selected thresholds; independently review
   execution and intent before merge readiness.

## Open decisions and recommendation boundaries

**Open product/technical policy:** calendar scope/filtering, all-day priority and
representation, tie/overflow selection; offline freshness/retention and cross-day
invalidation; stale-data/fallback artwork and optional-service/degraded behavior;
refresh interval/dead zone/content-change policy; filesystem-loss recovery and
formatting; power/current, battery/timing tolerances and hardware pilot acceptance.
Do not infer selection from existing constants or API behavior.

**Recommendations not yet selected:** Arduino CLI as the reproducible build route,
immutable compatible library pins, lightweight C++ host testing, explicit source
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

The accepted planning assessment is prior execution evidence, not independent
product authority. No historical firmware releases or earlier owner-policy decisions
are inferred from commit messages. Future material changes preserve truthful
supersession here and reconcile their current-state owner separately.

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
