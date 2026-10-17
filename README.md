# xiao-ee02-e-paper-calendar

Battery-powered EE02 five-day calendar and weather dashboard.

The [original prototype article](https://www.makerguides.com/e-paper-wall-calendar-with-ai-generated-artwork-and-weather-display/) describes the historical artwork-based firmware. AI artwork generation, processing, caching and rendering have been removed. The former 1024×1024 region is reserved for the future five-day calendar; its temporarily empty state is an intermediate development state, not a completed display design or independently releasable product outcome.

## Approved product direction (not yet implemented)

- **Display:** 1600×1200 Spectra 6, rolling next five days starting today, fixed waking-hour window (exact hours configurable), timed grid the leading candidate versus a five-day agenda. The week view **replaces the 1024×1024 artwork area**. Display layout is not finally selected.
- **Other panels:** retain weather, multi-day forecast, today's detailed events, battery indication, and their existing positions initially. Whether the legacy monthly calendar remains is Open.
- **Calendars:** select multiple source calendars; support Google, iCloud/CalDAV and general ICS by source-independent internal event representation. Entirely on-device fetching/normalization; **no separate backend, hosted processor or update preprocessor**. Sequential streaming ICS processing is selected; provider-access scope, including ICS-only initial access and private iCloud versus public ICS, remains Open.
- **Selected graphics/memory architecture:** Seeed_GFX2 with a locally maintained external-framebuffer ownership extension; one application-owned PSRAM workspace for sequential, one-source-at-a-time streaming ICS processing, then rendering. A separate fixed-capacity five-day PSRAM snapshot owns normalized occurrences, text and transactional source outcomes without references into the reused workspace. GFX2 borrows only the 960,000-byte framebuffer slice and must not free it or allocate an unnecessary second framebuffer. The 2 MiB workspace, 1,024 occurrences and 320 KiB snapshot are provisional evaluation budgets, not validated operating limits. Borrowing, a scoped workspace, synthetic snapshot publication, standalone HTTP framing/line unfolding, bounded property/component parsing, selective source-local dependency staging, typed primitive decoding, VEVENT-local checks, timezone/observance-local checks, source-local coverage, standalone core RECUR grammar and contextual RRULE validation are implemented in isolation; live calendar acquisition, complete component/source semantic validation and production phase reuse remain unimplemented, with hardware behavior unvalidated. Consult the [canonical architecture and validation contract](docs/ENGINEERING.md#selected-gfx2-psram-and-streaming-calendar-architecture).
- **Storage/time:** NVS stores changed persistent configuration only (Wi-Fi, timezone, calendar URLs/selection, refresh settings). No daily churn, events, weather or routine counters go to NVS. RTC-retained memory may hold validated compact sleep context; reconstruct after invalid RTC/reset. NTP on cold boot when required, then approximately daily; use internal RTC between syncs.
- **Refresh/recovery:** battery-powered, single user-configurable coordinated data-acquisition/render cycle, independently validated per source. Successful calendars display even if another calendar fails, with a named error bar; a weather failure is shown in its own panel while previously valid forecast data may remain. Three consecutive complete acquisition failures cause a full error screen; cold boot tries Wi-Fi before presenting recovery instructions.
- **Setup:** device-hosted Wi-Fi webpage (LAN while explicitly awake, or open/no-password AP). AP stops after 10 minutes without meaningful user activity or 30 minutes maximum, then sleeps. Entering a new password may show characters; after save it is masked and the saved value is never echoed. Calendar URL fields remain displayed. An operator-accessible hardware AP trigger is required; simultaneous Setup+Refresh hold for five seconds is a **proposed, unverified** factory-reset gesture, not yet a pin/wake contract.

This is the **approved design intent**, not an assertion of deployed features. Exact schema limits, recurrence correctness, framebuffer/DMA behavior, regional display behavior, battery runtime, and recovery button implementation require validation. See [docs/ENGINEERING.md](docs/ENGINEERING.md) for owned contracts, Open decisions, and testing.

## Current prototype implementation (source observed; not the redesign)

The current source wakes to show current weather, five-day weather forecast, a monthly calendar, up to six events from a Google Apps Script endpoint, and battery indication, preserving their existing positions and dividers. The former artwork region remains empty after the existing screen clear. Its existing sleep policy excludes 00:00–04:59 when local time is known. These are implementation observations, not approval of the final layout or retention of the legacy monthly calendar. This prototype has not been validated as a reproducible firmware build or on target hardware.

## Repository workflow

Retain the Seeed XIAO ESP32-S3 Plus/EE02 Arduino platform. Carry out the redesign as separately reviewable semantic changes with static and focused host behavior tests. Full firmware compilation and hardware validation remain owner-controlled milestones; compilation is preferred but not a prerequisite for initial repairs. Uncompiled code is not claimed firmware-compatible, and CI or hosted builds are not required.

See [AGENTS.md](AGENTS.md) for developer workflow, [docs/ENGINEERING.md](docs/ENGINEERING.md) for technical requirements, current-source boundaries and roadmap, and [CHANGELOG.md](CHANGELOG.md) for merge-facing history.

## Hardware

| Component | Detail |
|-----------|--------|
| **Board** | Seeed Studio XIAO ESP32S3 Plus |
| **Display** | XIAO EE02 Expansion Board (6-color e-paper, 1600×1200) |
| **Battery** | Li-Po battery (connected via XIAO battery header) |

## Current implementation — How It Works

1. **Wake from deep sleep** — Every `DEEP_SLEEP_HOURS` (default: 3 hours), or at 05:00 if the wake time would fall in the dead zone
2. **Connect to Wi-Fi** and sync time via NTP
3. **Fetch weather data** from OpenWeatherMap (current, 3-hourly forecast, and daily grouped forecast); after the data-acquisition Wi-Fi connection succeeds, each fetch is attempted independently in this order
4. **Fetch calendar events** from a Google Apps Script web app on the same connection, regardless of weather/forecast request results. Each fetch function is called once per connected acquisition; the existing calendar request retry remains internal to its fetch function. Source-specific failures are logged, and `updateWeather()` still returns current-weather success. If Wi-Fi fails, no fetch is attempted
5. **Render the display** — Initializes, rotates and clears the e-paper, draws the existing dividers, daily forecast, monthly calendar, events, battery and weather (including the 3-hourly forecast), then updates and sleeps the panel. No artwork requests, decoding, cache access or filesystem initialization remain
6. **Deep sleep** until the next cycle; with `DEEP_SLEEP_HOURS = 0`, setup runs once and the empty loop performs no further updates

## Current implementation — Layout

```
┌───────────────────────────────────────────────┐
│                    ┌─────────────────────────┐│
│                    │      Weather Panel      ││
│   Reserved for     │                         ││
│   future week view │  Day name, temperature  ││
│                    │  Weather icon (large)   ││
│                    │  Humidity, rain, wind   ││
│                    │                         ││
│                    │  3-hourly forecast (5×) ││
├────────────────────┼─────────────────────────┤│
│                    │    Monthly Calendar     ││
│   5-Day Forecast   │                         ││
│                    │    Calendar Events      ││
│                    │               [Bat]     ││
└───────────────────────────────────────────────┘
```

## Current implementation — Configuration

Current source uses [constants.h](constants.h) for settings, [driver.h](driver.h) for display selection and local `credentials.h` for secrets. These compile-time inputs will be superseded by device-hosted configuration for the approved redesign:

| Constant | Description |
|----------|-------------|
| `WX_LOCATION` | City and country for weather (e.g. `"Lohr am Main,DE"`) |
| `TIMEZONE` | POSIX timezone string for NTP sync |
| `DEEP_SLEEP_HOURS` | Hours between wake cycles (0 = disabled, for debugging) |

For an operator build, create a local `credentials.h` with your credentials and API keys. It is intentionally excluded from Git; keep real values out of commits, shared logs, and receipts. The following values are illustrative only:

```
const char *SSID = "xxx";
const char *PASSWORD = "xxx";
const char *OPENWEATHER_API_KEY = "xxx";
const char *GOOGLE_CALENDAR_URL = "https://script.google.com/macros/s/xxx";
const char *CALENDAR_API_KEY = "xxx";
```

## Arduino IDE Settings

These are the settings documented by the sketch. A complete successful firmware build from reproducibly pinned inputs has not been demonstrated. Do not interpret this table as verified toolchain or display-driver compatibility.

| Setting | Value |
|---------|-------|
| Board | XIAO_ESP32S3_PLUS |
| USB CDC on Boot | Enabled |
| PSRAM | OPI PSRAM |
| Partition Scheme | Default with spiffs (3MB APP/1.5MB SPIFFS) |
| ESP32 Core | 4.0.0-alpha1 |

The documented partition selection is unchanged. The application no longer mounts or uses SPIFFS; artwork retirement does not erase old flash contents, format storage or migrate partitions.

## Dependencies

These are documented inputs of the current prototype, not the selected target
graphics integration:

| Library | Documented version |
|---------|---------|
| [Seeed_GFX](https://github.com/Seeed-Studio/Seeed_GFX) (TFT_eSPI fork) | 2.0.3 |
| ArduinoJson | 7.4.3 |

A version number alone does not identify compatible source. The current Seeed_GFX/TFT_eSPI integration remains unresolved; the inspected V2.0.3 tag cannot be assumed to reproduce the intended display path. The selected target has a pinned GFX2 baseline with a local borrowing extension and isolated compile/link evidence below; production migration, runtime memory fit and a complete production build remain unverified. See the [engineering baseline](docs/ENGINEERING.md#known-engineering-limitations). PNGdec and OpenAI credentials are no longer application requirements; the core networking/TLS dependencies remain.

### Isolated GFX2 source baseline and smoke check

[third_party/Seeed_GFX2](third_party/Seeed_GFX2/library.properties) vendors
Seeed-Studio/Seeed_GFX2 commit
`8f74b7aa91c4fcbc5ba2467bab4de4e7ce0eeeeb` (upstream metadata version 1.0.0).
This is an approved source-integration pin, **not validated hardware compatibility**.
The complete upstream `src` hierarchy, `library.properties` and
[LICENSE](third_party/Seeed_GFX2/LICENSE) were byte-identical at import. Only
`src/panel/Panel_EPaper.h` and `.cpp` now carry the local borrowing extension;
all other vendor files and license notices retain their imported bytes.
The license includes Seeed's MIT terms and third-party notices; font notices remain
inside `src`. Upstream examples, tools, assets and Git metadata are excluded.
`Panel_EPaper::setBorrowedFrameBuffer(buffer, availableBytes)` registers caller-owned
writable storage before initialization, admits exactly the native 960,000-byte
slice, and never allocates/frees/replaces it. Explicit colorful configuration is
required; incompatible modes and effective mirroring are refused. Successful panel
`end()` forgets the pointer; rebegin requires registration again. BUSY/transport
failure cannot be treated as successful shutdown. Destruction never frees caller
memory and does not prove physical quiescence. Consult the
[canonical interface/lifetime contract](docs/ENGINEERING.md#local-t133a01-framebuffer-borrowing)
before use. Production still uses legacy Seeed_GFX/TFT_eSPI.

[Ee02PsramWorkspace](application/psram_workspace.h) now owns one caller-sized
PSRAM-only allocation with scoped direct `withScratch(offset, bytes, callback)`
access and a checked `render(draw)` handoff. It registers only the 960,000-byte
prefix, preserves operation/shutdown results separately and refuses reuse/free
after uncertain hardware shutdown, including on destruction. No recovery or
force-release API exists. Callbacks must finish users and discard aliases; C++
cannot prevent escaped pointers or concurrent writes. The isolated fixture uses
2 MiB only as a provisional evaluation parameter, verifies scratch-tail isolation,
and runs the ordinary owned case before workspace allocation. See the
[canonical workspace contract](docs/ENGINEERING.md#application-owned-psram-workspace).

[FiveDaySnapshot](application/five_day_snapshot.h) independently owns one PSRAM-only
allocation for the immutable five-day window, records, full UTF-8 titles/labels and
source outcomes. Timed UTC intervals/points and civil-date all-day intervals retain
their original values. A complete catalogue starts Pending; append transactions
hide staging, commit complete sources (including empty ones), or roll back only
the failed source as Incomplete. Validation/capacity errors automatically abort;
text is never truncated. Read-only records preserve committed input order.
The fixture uses synthetic inputs, 1,024 record slots, 16 sources and 262,144 text
bytes only as evaluation arguments (287,032 allocated bytes). The separate
source-local ICS stage is described below; this snapshot provides no timezone
resolver, persistent identity or production integration. See the
[canonical snapshot contract](docs/ENGINEERING.md#normalized-five-day-occurrence-snapshot).

The separate [compile/link fixture](tests/gfx2_baseline/gfx2_baseline.ino) selects
`Seeed_Product::Seeed_ePaper_13INCH3_C`, which the pinned catalog maps to EE02,
T133A01 and native 1200×1600 at four bits/pixel. It calls initialization, rotation,
six-color drawing, text, refresh and shutdown APIs for both owned and borrowed paths.
Borrowing uses official factory construction, an instance that outlives its external
facade, and explicit `PanelMode::Colorful`. It has no credentials or network
calls and is outside the production sketch's root/`src` compilation layout. The
vendor tree is also outside sketch `src`; pass it explicitly only to the smoke build.
Do not install it globally or pass it to the current production build as a substitute
for Seeed_GFX/TFT_eSPI.

**Modified fixture compile/link: PASS (2026-10-12).** The already available isolated
Arduino CLI 1.5.1 / official Arduino-ESP32 3.3.12 / GCC 14.2.0 toolchain compiled the
workspace handoff, synthetic snapshot operations and both graphics paths. The exact command below ran from the repository
root, with all configuration/cache/build outputs outside the checkout. The temporary
installation is environment evidence, not a portable installation guarantee; use
an equivalent already available exact toolchain, and inspect its board recipes before
substituting paths. No install/upgrade was performed for this snapshot change.

```sh
env ARDUINO_DIRECTORIES_DATA=/tmp/ee02-gfx2-compat.qWyDXC/data \
  /tmp/ee02-gfx2-compat.qWyDXC/bin/arduino-cli \
  --config-file /tmp/ee02-gfx2-compat.qWyDXC/arduino-cli.yaml \
  compile --clean --verbose --warnings all --jobs 2 \
  --fqbn esp32:esp32:XIAO_ESP32S3_Plus:PSRAM=opi,CDCOnBoot=default,PartitionScheme=default_8MB \
  --library /workspace/xiao-ee02-e-paper-calendar/third_party/Seeed_GFX2 \
  --build-path /tmp/ee02-five-day-snapshot-build \
  /workspace/xiao-ee02-e-paper-calendar/tests/gfx2_baseline \
  > /tmp/ee02-five-day-snapshot-compile.log 2>&1
```

Use the CLI's single-library `--library` option, not a relative header include that
bypasses library discovery. Inspect verbose output and linked implementation symbols
to confirm this vendored source, the intended board and T133A01 path. Reject unintended
GFX2 copies or legacy TFT_eSPI linkage; the path option alone is not proof of resolution.
The run verified the board-provided `ARDUINO_XIAO_ESP32S3_PLUS` macro and `qio_opi`
include path, one vendored GFX2 selection, snapshot/workspace handoff, EE02 factory/board,
borrowed API and T133A01 transfer/update symbols, with no legacy TFT_eSPI or host-shim
linkage. It reports 509,715/3,342,336
bytes flash and 24,720/327,680 bytes static RAM. These are compiler totals, not runtime
heap/PSRAM/stack headroom. Three inherited warnings remain at `Seeed_GFX.cpp:795`
(type limits) and `:695`/`:707` (possibly uninitialized); no unrelated fixes were made.
At import the fixture compile was NOT RUN; the subsequent unmodified disposable
experiment passed separately. Do not flash as part of this check. Source import
and compilation never establish physical EE02 behavior; see the
[canonical baseline evidence and boundaries](docs/ENGINEERING.md#vendored-gfx2-dependency-baseline).

### Standalone bounded ICS ingestion

[IcsIngestion](application/ics_ingestion.h) accepts raw HTTP/1.0 or HTTP/1.1
response bytes and explicit transport events in caller-owned scratch. It supports
exact Content-Length and HTTP/1.1 chunked framing with bounded extensions/trailers.
[IcsLogicalLines](application/ics_logical_lines.h) independently unfolds strict
CRLF plus one SPACE/HTAB, preserving other octets across arbitrary input chunks.
This ingestion layer performs no property/component validation or whole-feed storage.
The downstream structural parser validates syntax/hierarchy; selected VEVENT-local
checks now exist below, while complete event/source semantics remain unimplemented.

Only HTTP 200 with complete supported framing and explicit successful logical-line
finalization produces ingestion Complete. Empty framed bodies mean zero lines,
not a valid empty calendar. Staged line delivery is provisional; errors, truncation,
unsupported encodings, close-delimited bodies and resource exhaustion fail explicitly.
Redirects yield bounded untrusted Location handoff data; they are never followed.
URL resolution, origin/credential policy, HTTPS downgrade prevention, redirect-chain
budgets, certificates and actual socket/TLS binding remain separate future work.

Sessions allocate nothing and must live inside one workspace `withScratch()` scope;
callback views must not escape (C++ cannot revoke aliases). Capacities/time limits are arguments, with no product
defaults. Tests evaluate 8 MiB body/16 MiB received bytes, 4 KiB physical/16 KiB logical
lines and 120-second processing/10-second stalled-read budgets provisionally.
Strict C++11 and ASan/UBSan host checks passed; no new target compile, live provider
or physical EE02 evidence is claimed. Production Apps Script acquisition and the
snapshot/graphics implementations are unchanged. Consult the
[canonical ingestion contract](docs/ENGINEERING.md#bounded-streaming-ics-ingestion)
and [focused test commands](AGENTS.md#commands-and-validation).

### Bounded ICS property/component parser

[IcsStructure](application/ics_structure.h) validates complete unfolded octet spans
without allocating. It preserves property values and exposes quoted-aware encoded
parameter iterators, including unchanged RFC 6868 caret sequences. It validates
UTF-8, RFC 5545 structure and RFC 9073 PARTICIPANT/VLOCATION/VRESOURCE relationships
and child ordering. Callbacks carry the current component, parent ordinal, depth
and calendar ordinal; child UID/SUMMARY values never become enclosing event data.

Caller-owned scratch and callback views live within one workspace `withScratch()`
scope. All capacities are explicit; 839 scratch bytes for the host evaluation
profile is calculated from the actual layout, not measured device headroom.
Multiple calendar objects are bounded; root closure is provisional until explicit
finalization after ingestion Complete. Unknown calendar-level components are
opaque; unsupported placement and RFC 7953 availability fail as UnsupportedProfile.
Malformed nesting, invalid text, exhaustion and consumer failure are terminal.

Strict C++11 and ASan/UBSan host checks passed, including guarded actual-workspace
use. The extended isolated EE02 fixture compiled/linked using the existing CLI
1.5.1/core 3.3.12/OPI toolchain; this does not establish production compatibility
or runtime hardware behavior. No live feed, production ICS integration, recurrence/timezone
normalization or parser-driven snapshot publication is implemented. See the
[canonical contract](docs/ENGINEERING.md#bounded-ics-propertycomponent-parser)
and [host commands](AGENTS.md#commands-and-validation).

### Selective source-local ICS dependency staging

[IcsSourceStage](application/ics_source_stage.h) borrows checked component/property
tables and an append-only byte pool within one workspace `withScratch()` lifetime.
It owns no allocation. Complete selected event, recurrence, revision, cancellation,
detail and timezone values/encoded parameters survive transient parser callbacks.
Known identifiers use compact codes; unknown names remain complete. No full-feed
String/document, UID index, decoded duplicate, normalization or publication exists.

Declared non-event values are explicitly projected out. Unknown opaque components
already accepted by `IcsStructure` keep a descriptor/name and checked property
count, with no payload/property descriptors; they do not invalidate supported events.
Relevant unknown properties/parameters remain bounded, copied and marked for review.
Exhaustion fails the source. Missing, original empty and omitted values are distinct.
`finish(ingestion, structure)` requires corresponding complete framing/structure and
matching counts. `StagedSuccessfully` means retention completed, not a semantically
complete schedule. No parser-driven `FiveDaySnapshot` transaction is invoked.

Strict C++11 and ASan/UBSan staging/workspace checks passed; target evidence and the
exact retention/disposition/lifetime contract are in
[ENGINEERING](docs/ENGINEERING.md#selective-source-local-ics-dependency-staging).
The 4,096 components, 8,192 stored properties, 1 MiB pool, 256 review items and
64 KiB review-byte allowance are evaluation inputs only. Production acquisition,
complete semantic processing and physical runtime memory remain unvalidated/unintegrated.

### Typed ICS primitive values

[IcsTypedValues](application/ics_typed_values.h) reads successfully staged properties
without changing source bytes, dispositions or review flags. Allocation-free C++11
functions decode DATE/DATE-TIME calendar fields, DURATION components, PERIOD,
INTEGER and UTC-OFFSET. Bounded writers decode scalar TEXT and RFC 6868 parameter
members into temporary caller storage, with explicit optional termination and no
partial writes or truncation. Property-local VALUE/TZID checks and bounded RDATE/
EXDATE cursors retain floating/UTC/TZID distinctions. Exhaust the cursor through End
and check its final status; individual members are provisional.

Leap-second confirmation and unresolved period ordering return NeedsContext.
No timezone resolution, epoch conversion, RECUR interpretation by this typed API,
component acceptance or snapshot publication occurs. Omitted payloads remain unavailable, not empty;
all borrowed spans expire on source release or workspace reuse. See the
[typed contract and executed evidence](docs/ENGINEERING.md#typed-ics-primitive-values)
and [host commands](AGENTS.md#commands-and-validation).

### VEVENT-local validation

[IcsEventLocal](application/ics_event_local.h) performs bounded read-only checks on
one explicit Calendar/Event pair in a readable stage. It checks association/property
links, singleton cardinality, required UID/DTSTAMP, METHOD-dependent DTSTART,
DTEND/DURATION exclusion, selected typed fields and locally decidable endpoint/
duration relationships. METHOD is presence only; its tokens are not interpreted.

`LocalChecksPassed` always retains source-validation and identity-reconciliation
obligations. Recurrence, METHOD, timezone/local ordering, leap seconds, extensions
and unchecked optional fields have explicit pending bits. No event object, revision
selection, timezone conversion, normalized occurrence or snapshot transaction is
created. Limits are caller-supplied evaluation controls. All borrowed processing
ends before stage release/workspace reuse; Report contains no pointers.
See the [local contract and evidence](docs/ENGINEERING.md#vevent-local-validation)
and [focused host commands](AGENTS.md#commands-and-validation).

### VTIMEZONE and observance-local validation

[IcsTimezoneLocal](application/ics_timezone_local.h) validates one explicit
Calendar/Timezone or Timezone/Standard-or-Daylight pair in readable staged data.
It checks association, direct property links, selected cardinality, nonempty TEXT
TZID, UTC LAST-MODIFIED, local observance DTSTART and individual UTC offsets.
Repeatable observance RDATE lists must finish completely within explicit member/
byte limits. RRULE and observance EXDATE semantics remain deferred; approved
TZNAME/COMMENT omissions are respected rather than decoded as empty values.

`LocalChecksPassed` always retains source-validation and transition-resolution
obligations; timezone identity, recurrence, extensions and other applicable work
remain pending. No child enumeration, TZID matching, external lookup, transition
calculation, event conversion, source acceptance or snapshot transaction occurs.
The stateless allocation-free calls and all borrowed processing end before stage
release/workspace reuse. See the [contract and executed evidence](docs/ENGINEERING.md#vtimezone-and-observance-local-validation)
and [focused commands](AGENTS.md#commands-and-validation).

### Source-local validation coverage

[IcsSourceLocal](application/ics_source_local.h) inspects every readable staged
component in source order across all calendar objects. It checks associations,
root metadata property chains and explicit budgets, invoking the accepted VEVENT,
VTIMEZONE and STANDARD/DAYLIGHT local validators. Separate original deferred masks
and first-failure local diagnostics survive without semantic reinterpretation.
Retained RFC 9073 children remain pending review; projected non-event and opaque
components are observed without treating their payloads as event data.

`LocalCoveragePassed` means the bounded enumeration and selected local checks
finished, always with SourceSemanticsPending. Zero-event and opaque-only sources
remain subject to later source-policy/semantic validation. There is no allocation,
identity matching, recurrence/timezone processing, normalized event model, source
transaction or snapshot call. Reports own no staged references; borrowed processing
finishes before release/workspace reuse. See the [coverage contract and fresh evidence](docs/ENGINEERING.md#source-local-validation-coverage)
and [focused commands](AGENTS.md#commands-and-validation).

### Bounded RECUR core grammar

[IcsRecurValue](application/ics_recur_value.h) validates one complete length-delimited
RRULE value without allocation or mutation. It checks RFC 5545 core rule parts in
any order, uniqueness, required FREQ, typed UNTIL, numeric/list bounds and locally
decidable cross-part constraints. Caller limits bound bytes, parts and aggregate
BY-list members. Reports contain only status, offset, frequency, presence bits and
pending leap-second evidence; failed reports clear parsed fields.

Well-framed unknown parts and RFC 7529 RSCALE/SKIP return UnsupportedFeature before
Gregorian-specific value checks. CoreGrammarValid is representation evidence only:
this API does not check DTSTART/UNTIL compatibility. The separate contextual API
below now checks those representations; expansion support, multiple-rule reconciliation,
exclusions, overrides, timezones and source completeness remain deferred. Existing
event/timezone/source-local validators do not invoke the grammar API. Obtain a
readable retained RRULE using stage readers and finish validation before stage
release/workspace reuse. See the
[grammar contract and evidence](docs/ENGINEERING.md#bounded-recur-core-grammar-validation)
and [focused commands](AGENTS.md#commands-and-validation).

### Contextual RRULE validation

[IcsRecurContext](application/ics_recur_context.h) validates one explicitly supplied
retained RRULE in its actual VEVENT or STANDARD/DAYLIGHT parent context. It requires
the applicable existing local checks, verifies the complete direct property chain,
and composes the core grammar validator. Selected DTSTART/UNTIL type and UTC/local
relationships are enforced without comparing dates or converting timezones.

For DATE DTSTART, valid BYSECOND/BYMINUTE/BYHOUR parts are reported as ignored under
RFC 5545; source bytes remain unchanged. Any RRULE parameters and multiple rules
remain pending review/reconciliation. A METHOD-bearing event without DTSTART returns
MissingStartContext only after grammar passes. ContextChecksPassed always retains
expansion and recurrence-set obligations; neither outcome establishes executable
recurrence, source completeness or snapshot eligibility. Reports own no staged
references, all reads finish before scratch reuse, and no allocation or production
integration is added. See the [context contract and evidence](docs/ENGINEERING.md#contextual-rrule-validation)
and [focused commands](AGENTS.md#commands-and-validation).

## Current implementation — Google Calendar Setup

The [google-calendar-api.gs](google-calendar-api.gs) file exposes today's calendar events as a JSON endpoint. For an authorized deployment, create a Google Apps Script project, set its `API_KEY` to a private value matching `CALENDAR_API_KEY`, and deploy as a web app using the identity/access settings described in the script. Set its URL in `GOOGLE_CALENDAR_URL` in local `credentials.h`.

The documented deployment executes as the deploying account and allows access to anyone possessing the endpoint/key. Review that account's calendar access and deployment timezone before use; timezone alignment with the firmware has not been verified. Current code enumerates all accessible calendars, returns at most eight events, and represents all-day events as `00:00`–`24:00`. Firmware retains at most six before sorting that retained subset. The script's comments describing six returned events and `--:--` placeholders disagree with its code; those comments are not an approved policy.

## Operation and recovery limits

There is no committed reproducible build/flash procedure or validated recovery runbook. The intended AP/web/factory-reset recovery described above is not yet implemented. A focused [host regression](tests/data_acquisition_test.cpp) covers acquisition orchestration; it does not validate Arduino integration, display geometry, memory peaks or hardware. Keep local credentials private and use Serial output for diagnosis without sharing sensitive event/API content. Calendar retrieval is independent of weather success once acquisition Wi-Fi connects, but event data remains volatile across deep sleep. No application filesystem operations or persistent data cache remain; do not rely on offline retention. TLS certificate verification is currently disabled; security stabilization and owner-controlled hardware validation are still required. The redesign's selected recovery and storage policies are in the engineering anchor; implementation and hardware results remain unverified.

## License

See [LICENSE](LICENSE).
