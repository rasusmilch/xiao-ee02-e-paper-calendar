# xiao-ee02-e-paper-calendar

Battery-powered EE02 five-day calendar and weather dashboard.

The [root sketch](xiao-ee02-e-paper-calendar.ino) is the canonical integrated GFX2
application: authenticated weather and selected HTTPS ICS calendars, one battery
measurement, one dashboard composition and checked timer deep sleep. It loads validated version-one NVS user settings, uses build-only CA/limits and fresh
SNTP on each wake. Source and host validation
are implemented; target compilation, actual services, physical EE02 and battery-life
readiness remain unverified. The independent deterministic preview uses synthetic data.
The former opt-in live sketch has been promoted into the root and its tracked entry
point/template retired, rather than maintained as another application.

The [original prototype article](https://www.makerguides.com/e-paper-wall-calendar-with-ai-generated-artwork-and-weather-display/)
and retained legacy headers/Apps Script are historical reference only. The active
root has no TFT_eSPI, Apps Script, monthly-grid or six-event execution path. AI
artwork and its processing/cache have been removed. Monthly-grid retention and final
layout remain Open; the unassigned former month area is blank in the canonical UI.

## Approved product direction (remaining policies and hardware validation pending)

- **Display:** 1600×1200 Spectra 6, rolling next five days starting today, fixed waking-hour window (exact hours configurable), timed grid the leading candidate versus a five-day agenda. The week view **replaces the 1024×1024 artwork area**. Display layout is not finally selected.
- **Other panels:** retain weather, short and multi-day forecasts, today's detailed events, battery indication, and their existing positions initially. Whether the legacy monthly calendar remains is Open.
- **Calendars:** select multiple source calendars; support Google, iCloud/CalDAV and general ICS by source-independent internal event representation. Entirely on-device fetching/normalization; **no separate backend, hosted processor or update preprocessor**. Sequential streaming ICS processing is selected; provider-access scope, including ICS-only initial access and private iCloud versus public ICS, remains Open.
- **Selected graphics/memory architecture:** Seeed_GFX2 with a locally maintained external-framebuffer ownership extension; one application-owned PSRAM workspace for sequential, one-source-at-a-time streaming ICS processing, then rendering. A separate fixed-capacity five-day PSRAM snapshot owns normalized occurrences, text and transactional source outcomes without references into the reused workspace. GFX2 borrows only the 960,000-byte framebuffer slice and must not free it or allocate an unnecessary second framebuffer. The 2 MiB workspace, 1,024 occurrences and 320 KiB snapshot are provisional evaluation budgets, not validated operating limits. Borrowing, a scoped workspace, synthetic snapshot publication, standalone HTTP framing/line unfolding, bounded property/component parsing, selective source-local dependency staging, typed primitive decoding, VEVENT-local checks, timezone/observance-local checks, source-local coverage, standalone core RECUR grammar, contextual RRULE validation, VEVENT recurrence-property coverage, observance recurrence-local coverage, source-wide recurrence-local coverage, selected TZID reference-local identity matching, source-wide VEVENT TZID identity coverage, a restricted nonrecurring ICS-to-snapshot producer, a reusable snapshot renderer with a dedicated deterministic EE02 preview, an authenticated streaming HTTPS acquisition API, a rolling local five-day window builder and a multi-calendar refresh coordinator and a read-only dashboard composition are implemented and composed by the canonical root. General source semantics, the settings webpage/AP, final RTC/recovery policies and target/hardware validation remain incomplete. Consult the [canonical architecture and validation contract](docs/ENGINEERING.md#selected-gfx2-psram-and-streaming-calendar-architecture).
- **Storage/time:** NVS stores changed persistent configuration only (Wi-Fi, timezone, calendar URLs/selection, refresh settings). No daily churn, events, weather or routine counters go to NVS. RTC-retained memory may hold validated compact sleep context; reconstruct after invalid RTC/reset. NTP on cold boot when required, then approximately daily; use internal RTC between syncs.
- **Refresh/recovery:** battery-powered, single user-configurable coordinated data-acquisition/render cycle, independently validated per source. Successful calendars display even if another calendar fails, with a named error bar; a weather failure is shown in its own panel while previously valid forecast data may remain. Three consecutive complete acquisition failures cause a full error screen; cold boot tries Wi-Fi before presenting recovery instructions.
- **Setup:** device-hosted Wi-Fi webpage (LAN while explicitly awake, or open/no-password AP). AP stops after 10 minutes without meaningful user activity or 30 minutes maximum, then sleeps. Entering a new password may show characters; after save it is masked and the saved value is never echoed. Calendar URL fields remain displayed. An operator-accessible hardware AP trigger is required; simultaneous Setup+Refresh hold for five seconds is a **proposed, unverified** factory-reset gesture, not yet a pin/wake contract.

This is the **approved design intent**, not an assertion of deployed features. Exact schema limits, recurrence correctness, framebuffer/DMA behavior, regional display behavior, battery runtime, and recovery button implementation require validation. See [docs/ENGINEERING.md](docs/ENGINEERING.md) for owned contracts, Open decisions, and testing.

## Active application and interim policies

The root reuses the reviewed integrated refresh path. It deliberately replaces the
legacy application rather than offering a compatibility mode. Each admitted wake
uses one Wi-Fi connection and a fresh observed SNTP notification, then independent
current/short/daily weather attempts and sequential calendar acquisition. The snapshot
keeps successful calendars when others fail. The dashboard consumes the existing
weather arrays and independently owned snapshot; unavailable observations and source
failures remain visible. Unsupported recurrence, floating/TZID event times, overrides,
cancellations and other unresolved semantics make affected calendars Incomplete.

Fresh synchronization **on every wake** and the explicitly configured sleep seconds
are conservative interim implementation choices, not the final approximately-daily
NTP/RTC policy or an approved refresh default. Their energy cost is unmeasured. There
is no RTC-retained application state, event cache, settings webpage, recovery AP or
three-consecutive-failure screen. Those selected requirements remain unimplemented.

## Repository workflow

Retain the Seeed XIAO ESP32-S3 Plus/EE02 Arduino platform. Carry out the redesign as separately reviewable semantic changes with static and focused host behavior tests. Full firmware compilation and hardware validation remain owner-controlled milestones; compilation is preferred but not a prerequisite for initial repairs. Uncompiled code is not claimed firmware-compatible, and CI or hosted builds are not required.

See [AGENTS.md](AGENTS.md) for developer workflow, [docs/ENGINEERING.md](docs/ENGINEERING.md) for technical requirements, current-source boundaries and roadmap, and [CHANGELOG.md](CHANGELOG.md) for merge-facing history.

## Hardware

| Component | Detail |
|-----------|--------|
| **Board** | Seeed Studio XIAO ESP32S3 Plus |
| **Display** | XIAO EE02 Expansion Board (6-color e-paper, 1600×1200) |
| **Battery** | Li-Po battery (connected via XIAO battery header) |

## Current implementation — Boot to sleep

1. Admit the build, then load and validate the NVS settings record read-only. Missing,
   corrupt, unsupported or unreadable settings perform no acquisition or configured sleep;
   no default record, automatic erase or compile-time user-value fallback is used.
2. Connect Wi-Fi once and register an immediate-mode SNTP notification before starting
   synchronization. Require an event from this attempt, stop/unregister SNTP and apply
   the configured POSIX TZ. Stale/plausible time alone cannot authorize HTTPS or dates.
3. Verify the rolling local five-day window, allocate one PSRAM workspace and attempt
   authenticated current weather, short forecast and daily forecast independently.
4. Run FiveDayCalendarRefresh: compact enabled sources, initialize one independent
   transactional snapshot and stream selected HTTPS ICS sources sequentially. No
   Apps Script/backend or full ICS document buffer is used. Zero selection is valid.
5. Measure battery once and disable the divider. Close clients, stop SNTP, disconnect
   and verify Wi-Fi off. A shared calendar interruption does not produce a false schedule.
6. Poison/reuse scratch, borrow the framebuffer and compose the calendar, today details,
   weather, both forecasts, battery and source/overflow indications. Perform at most
   one refresh; check the operation and panel shutdown separately. No DEVELOPMENT label
   or old monthly grid is drawn. Geometry and final agenda/grid choice remain provisional.
7. Release healthy workspace/snapshot storage, configure timer wakeup with checked
   seconds-to-microseconds arithmetic and require ESP_OK before entering deep sleep.
   The interval is sleep time after this cycle, not a fixed wall-clock cadence.

Wi-Fi/time/window/PSRAM/shared-calendar failures can use the configured retry sleep
only after verified network shutdown and legal resource release, without fabricated
dates or a new error screen. Renderer failure can retry after checked healthy panel
shutdown. Unconfirmed network shutdown, Retained workspace, unchecked/failed panel
shutdown or failed resource release forbid sleep. Timer rejection also leaves the
application awake. The empty loop does no polling/retry; no automatic recovery policy
is invented for these blocked conditions. Software checks do not prove physical
power removal or sleep current.

## Persistent user settings and build-only inputs

[DeviceSettings](application/device_settings.h), its [version-one codec](application/device_settings_codec.h)
and [NVS service](application/device_settings_nvs.h) own the runtime settings used by
the root: Wi-Fi SSID/password, POSIX timezone, NTP server, enabled calendar names/full
HTTPS ICS URLs, weather enable/location/key and positive refresh seconds. No user
field falls back to device_config.h, credentials.h or historical constants. The
root loads the single ee02_settings/config blob read-only on every wake. Unconfigured,
corrupt, future-version and storage failures prevent acquisition and guessed dates.
There is no first-boot webpage/AP or automatic provisioning/import yet; a future
reviewed settings service will call this API. First-boot onboarding is incomplete.

All strings are owned, bounded byte values. V1 bounds excluding NUL are SSID 32,
password 64, timezone 127, NTP hostname 253, weather location 127, weather key 128,
calendar label 64 and URL 512, with at most eight configured calendars and a 5,632-byte
encoded bound (maximum legal encoded shape 5,417 bytes). These are implementation
bounds, not final product capacity/headroom claims. Enabled weather also must fit the
existing encoded-location/request budgets; no field is silently truncated. Wi-Fi
passwords are empty for an intentionally open network, 8–63 printable ASCII bytes,
or a 64-hex-digit PSK. Labels use existing UTF-8 validation. Nonempty calendar URLs
use existing HTTPS admission; disabled blank URLs and zero selected calendars are valid.
POSIX syntax remains a configuration responsibility; the root still checks actual
runtime timezone/window conversions before acquisition.

Service::save validates a whole candidate, compares deterministic bytes, skips equal
updates and replaces one blob, commits and reads it back before replacing active
values. Invalid candidates cause no storage access. Normal wake/acquisition/display/
sleep never saves. Explicit clear removes only this key, not the partition or other
namespaces. Errors never trigger automatic clearing. A reported write/commit/verify
failure preserves active RAM values, but flash may already contain the complete old
or new record: explicitly reload before assuming disk state. CRC32 detects accidental
corruption; it is not authentication, encryption or tamper protection.

The presentation API returns no saved Wi-Fi password/API key or raw blob. It exposes
secret-presence flags and requires explicit Keep/Replace/Clear edits. Keep/Clear must
carry no text; an eight-asterisk mask is reserved and rejected as a replacement.
Calendar URLs deliberately remain fully visible, including token-bearing queries:
future UI access can therefore disclose calendar access. The selected open AP policy
has no implemented session/network controls yet. Do not log URLs, keys, passwords,
CA material or raw requests; no physical flash confidentiality is claimed.

Copy [device_config.example.h](device_config.example.h) to ignored device_config.h
for build-only opt-in, separate calendar/weather CA PEM, provisional workspace/snapshot/
source capacities, calendar limits and finite Wi-Fi/NTP wait ceilings. It is inert
without deliberate opt-in/trust. Update any existing private header manually to this
revised interface; old user fields are ignored and never imported into NVS. Ignored
files are preserved unread/unmodified. CA provisioning/rotation remains Open and is
not persisted as user settings. No certificate or insecure fallback is supplied.

Refresh seconds come exclusively from NVS and are checked for microsecond overflow
and signed IDF duration range; the timer API separately checks the actual slow-clock
hardware limit. No shorter/default interval is substituted. Fresh SNTP on each wake
remains interim; RTC retention/daily NTP, final refresh defaults and recovery remain
unimplemented. The service does not initialize/reformat NVS. The inspected Arduino
core itself can erase NVS during pre-setup initialization for NO_FREE_PAGES or
NEW_VERSION_FOUND; that upstream behavior is unchanged and is not a service recovery
operation. Actual partition occupancy, flash interruption and endurance remain unverified.

Strict C++11 and ASan/UBSan checks execute 539 codec/storage/presentation cases and
40 actual-root wake cases, plus relevant calendar, weather, dashboard and workspace
regressions. Scripted storage failures do not prove physical power-loss recovery;
scripted decoded weather is not ArduinoJson execution. See [commands](AGENTS.md#commands-and-validation)
and the [storage contract](docs/ENGINEERING.md#versioned-nvs-device-settings).

## Arduino/toolchain and dependencies

The active source requires XIAO ESP32-S3 Plus, OPI PSRAM, inspected Arduino-ESP32
3.3.12 APIs, Core Debug Level None, ArduinoJson 7.4.3 and the explicitly selected
[pinned vendored Seeed_GFX2](third_party/Seeed_GFX2/library.properties). Core Debug None
is required because upstream HTTP/DNS diagnostics can disclose credential-bearing
requests; independent platform logging still needs review. These requirements are
not target compatibility proof. No package installation/upgrade or board/partition
change accompanied root promotion. Historical alpha-core/TFT_eSPI settings do not
apply to the new root. The documented Default with spiffs partition setting is not
a claim of SPIFFS use; no application filesystem or flash migration is performed.

Use the existing explicit GFX2 library-selection procedure below for future
owner-authorized root compilation (replace the isolated sketch path with this root,
and keep build output outside the repository). Do not select a global legacy GFX copy.
A complete root target build, physical EE02, TLS interoperability, actual timer wake,
PSRAM headroom, ADC calibration, panel sleep current and energy remain owner-deferred.
The host fixture executes the actual root using public synthetic config; weather
success branches use supplied decoded values, not real ArduinoJson parsing.

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
before use. The canonical root now uses this GFX2 borrowing path; legacy graphics
headers are inactive reference material.

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

### Rolling local five-day window

[FiveDayWindow](application/five_day_window.h) provides
`build(nowUtc, clockTrusted, configuredPosixTimezone, outputWindow)`. The caller
supplies UTC POSIX seconds, explicitly asserts trusted time, configures a nonempty
POSIX TZ string with setenv/tzset, and excludes timezone changes throughout the call.
The expected string must exactly match active TZ. Untrusted time, missing/mismatched
TZ or failed conversion leaves output byte-for-byte unchanged; there is no ambient
UTC fallback, system-clock lookup, automatic synchronization or configuration change.

The first civil date is **today locally**, followed by four dates. Six independently
resolved local-midnight UTC boundaries include Day 5 as the exclusive end. The
builder tries tm_isdst -1/0/1, round-trips each candidate, chooses the earliest valid
repeated midnight and refuses nonexistent midnight instead of normalizing to 01:00.
It validates consecutive dates, increasing boundaries, preceding dates, now containment
and checked civil/time_t ranges before one final assignment with reserved zero.
Local days need not be 86,400 seconds; supported half-hour transitions also work.

The small [shared time helper](application/five_day_local_time.h) supplies the existing
renderer/POSIX adapter's civil arithmetic and checked UTC conversion without graphics
headers. FiveDayPosixTime::convert remains the renderer callback. The builder owns
no snapshot, event records, allocations, cache, network or persistent state. The
existing snapshot and UTC/DATE event representations remain unchanged. POSIX TZ
syntax validation belongs to configuration; matching TZ plus round-trip checks do
not prove arbitrary rule-string validity.

The example preview builds its established October 23–27, 2026 window using fixed,
explicitly trusted example UTC time 1792746000 and checks the original boundary
fixture before allocating/populating a snapshot. It does not read current system time.
Host tests cover fixed/DST offsets, calendar rollovers, repeated/skipped midnight,
failure atomicity, allocation audits and real snapshot/ICS-producer/renderer
composition. Pure time cases also compile without graphics. Arduino/Newlib timezone
behavior, physical EE02, RTC validation and memory/power remain owner-deferred and
unverified. The canonical root now supplies fresh trusted UTC/configured TZ and
uses the resulting rolling dates through the coordinator. Final RTC/daily-sync
policy remains separate. See the
[window contract](docs/ENGINEERING.md#rolling-local-five-day-window) and
[host commands](AGENTS.md#commands-and-validation).

### Authenticated streaming ICS HTTPS acquisition

[IcsHttpsSource](application/ics_https_source.h) exposes
`acquire(urlView, snapshot, sourceIndex, workspace, limits, transport, clock,
clockContext, prerequisites)`. The caller supplies an initialized independent snapshot,
a selected Pending source, an allocated scratch-ready workspace, Wi-Fi readiness,
explicitly verified certificate-validation wall time, a readable NUL-terminated CA
PEM span (length includes the terminator), and monotonic milliseconds. Inputs and
transport/clock contexts must remain valid outside the workspace until return.
No URL, request target, token, event text or redirect Location is logged or returned.
Treat the entire URL as sensitive, including its hostname.

The narrow URL profile accepts case-insensitive HTTPS, ASCII DNS labels, optional
numeric port 1–65535, and encoded absolute path/query. Empty paths become `/`;
query-only resources become `/?query`. Encoded bytes are preserved exactly.
Hostnames must contain an ASCII letter. Userinfo, IPv6 literals, fragments, controls/whitespace,
backslashes and malformed percent escapes fail. One bounded HTTP/1.1 GET sends
Host (with explicit port), Accept:text/calendar, Accept-Encoding:identity and
Connection:close. There are no cookies, authorization headers, redirects or retries.

[IcsHttpsArduino](application/ics_https_arduino.h) binds the byte-I/O callbacks to the
inspected Arduino-ESP32 **3.3.12** NetworkClientSecure API. It resolves once with
synchronous `Network.hostByName`, then connects to the resolved IP while passing the
original hostname for certificate verification and SNI. CA trust is required;
there is no insecure or HTTP fallback. Keep the CA alive until acquisition returns.
The adapter requires Core Debug Level **None** because upstream DNS failure logging
can disclose the hostname; it does not change global logging or dependencies.

All limits are explicit evaluation arguments. Positive `socketMs` bounds the pinned
client's TCP connect and socket read/write operations; `handshakeSeconds` uses seconds
with checked conversion bounds. Partial writes and empty polls have finite inactivity
limits, and empty polls yield. Response inactivity uses `ingestion.stalledMs`, resets
on received progress and excludes synchronous processing of received bytes.
The live path requires `ingestion.processingMs == 0`, disabling only ingestion's total
elapsed budget; other callers retain the existing positive-budget behavior.
Synchronous DNS is uncancellable and has no caller timeout. There is **no overall
acquisition deadline**: successful DNS/TLS and progressing downloads remain accepted
regardless of total duration. A slow trickle can therefore prolong wake time/energy use.
No final refresh or energy policy is selected.

One `withScratch` scope partitions checked disjoint receive, HTTP, parser, stage and
producer decoding slices. Before DNS/TLS or GET transmission, component-owned
stage/parser initialization and `IcsIngestion::validateConfiguration` admit limits,
spans and capacity, including the nested line-unfolder slice. Configuration errors
and insufficient workspace leave the selected source Pending without network I/O.
The same scoped stage/parser pair handles the response. The clock-free ingestion
check starts no session; actual ingestion initialization starts its response timer
only after DNS, TLS and the complete GET write succeed.
Raw bytes pass through existing ingestion → structure →
stage → unchanged nonrecurring producer → snapshot, without a full-feed buffer.
Observed declared HTTP boundaries permit finalization without waiting for EOF;
observed surplus bytes still fail. RedirectRequired closes the socket and leaves the
selected eligible source Incomplete without following or exposing Location.
Upstream failures use existing beginSource/failSource; the producer owns successful
publication and semantic rollback. Conflicting transactions remain intact. Invalid
arguments/state leave the selected source untouched. Earlier completed sources survive.
All socket/parser/stage users finish before workspace reuse; only snapshot-owned
records/text survive. A pointer-free Report carries sanitized layer statuses, resource
categories and committed counts; it does not replace snapshot outcomes.

Strict C++11 and ASan/UBSan host tests exercise the actual streaming pipeline with
scripted secure I/O, slow success, stalls, failures, scratch poisoning and renderer
traces. Adapter stand-ins verify API call ordering, not real DNS/TLS authentication.
Arduino compilation, physical EE02 testing and runtime/power measurements are
owner-deferred and unverified; no live feed or credential was used. The restricted
producer profile below is unchanged. Production networking/graphics, settings,
clock provisioning and coordinated dashboard integration remain later work; the
calendar-only coordinator below now composes rolling windows and source acquisition.
This capability is not a verified live wall calendar or provider interoperability
claim. See [engineering](docs/ENGINEERING.md#authenticated-streaming-https-ics-acquisition)
and [host commands](AGENTS.md#commands-and-validation).

### Multi-calendar refresh

[FiveDayCalendarRefresh](application/five_day_calendar_refresh.h) exposes synchronous
`refresh(nowUtc, clockTrusted, configuredTimezone, workspace, snapshot, sources,
sourceCount, limits, wifiReady, caPem, transport, clock, clockContext, callback,
callbackContext)`. Supply an uninitialized snapshot, a scratch-ready application
workspace, trusted UTC, and the same explicit POSIX TZ already configured by tzset.
Window failure initializes no snapshot and makes no calendar request. Record/text
capacities, configured-source count bound and nested HTTPS limits are caller inputs,
not final operating capacities.

Source descriptors contain enabled, display label and HTTPS URL view. Enabled
sources compact in their original order into consecutive snapshot indices; callbacks
also identify the original configuration index. Disabled sources consume no slots,
requests or warnings. Snapshot initialization copies enabled UTF-8 labels through
temporary aligned workspace descriptors; labels, URLs and input descriptors must
remain outside workspace throughout the call. URLs are never stored in the snapshot
or returned/logged. No additional persistent source/event model is introduced.

Each enabled source uses the actual streaming HTTPS/ICS producer sequentially with
one reusable transport. Failed sources become Incomplete, later sources are still
attempted and earlier Complete records survive. Invalid selected URLs make no request
and become Incomplete; valid outside-window-only sources are Complete with zero
records. Normal results distinguish AllComplete, Partial, NoneComplete and NoSelection,
and leave no selected source Pending. Shared invalid limits/state/storage interrupt
refresh with original acquisition diagnostics and remaining Pending counts; these
are not completed schedules. Admission failure preserves existing caller state
and never initializes a false schedule.
The optional callback observes scalar diagnostics only and must not reenter, mutate
snapshot/workspace, change TZ or retain borrowed references.

`acquisitionComplete` permits the caller's later display handoff: all network/parser
users have ended and workspace is Scratch. The existing renderer and POSIX converter
show combined events, source labels and partial-failure warnings. Zero selected
sources initialize a zero-source snapshot and show **No calendars selected**, while
keeping all five dates. Complete empty calendars and unavailable sources retain
their distinct messages. The coordinator does not render or schedule refreshes.

The former live-dashboard sketch is now the canonical
[root application](xiao-ee02-e-paper-calendar.ino); its tracked duplicate entry point
and example configuration have been retired. The current configuration/lifecycle are
specified above. Ignored old private demonstration files are preserved without being
read, deleted or automatically imported. The deterministic synthetic preview remains
independent. The actual-root host test composes real calendar HTTP/ICS publication,
source failures, poisoning and dashboard/panel ownership with scripted network/SNTP/
ADC/sleep and decoded-weather stand-ins; it is not target, cryptographic or JSON-parser
proof. See [AGENTS.md](AGENTS.md#commands-and-validation) for commands.

### Restricted nonrecurring ICS-to-snapshot producer

[IcsNonrecurringSnapshot](application/ics_nonrecurring_snapshot.h) connects the
accepted staged ICS path to a selected Pending snapshot source. It calls
IcsSourceLocal once, then validates the complete closed profile, checks all decoded
UID pairs, normalizes occurrences and completes one source transaction. Local
coverage alone never establishes source completion. Unsupported input or exhaustion
ends the selected transaction as Incomplete; tentative records disappear and earlier
Complete sources survive. Snapshot insertion's automatic rollback is respected.

The passive-feed profile admits one VCALENDAR with one or more direct VEVENTs:

- Root: singleton VERSION (2.0 or 2.0;2.0), nonempty decoded PRODID, optional
  CALSCALE:GREGORIAN and METHOD:PUBLISH, plus exactly named X-WR-CALNAME,
  X-WR-CALDESC and X-WR-TIMEZONE. Selected X-WR values are bounded valid TEXT;
  all root parameters, duplicates and other root properties are rejected.
  PUBLISH is passive feed metadata, not iTIP processing. X-WR-TIMEZONE never
  configures the display or resolves an event time; root metadata is not stored.
- Event: required UID, UTC DTSTAMP and DTSTART (DATE or UTC DATE-TIME); optional
  DTEND and SUMMARY. CREATED/LAST-MODIFIED require UTC without leap seconds;
  SEQUENCE is a nonnegative typed integer, never revision precedence. DESCRIPTION
  and LOCATION are validated TEXT and discarded. URL is a bounded absolute ASCII
  URI subset, never followed. TRANSP accepts OPAQUE/TRANSPARENT without hiding an
  event; STATUS accepts only CONFIRMED; CLASS accepts only PUBLIC. All are
  singletons. Missing/empty SUMMARY stays empty; ignored metadata never changes it.
- DTSTART/DTEND allow singular VALUE=DATE or VALUE=DATE-TIME. SUMMARY, DESCRIPTION
  and LOCATION allow one LANGUAGE member: 2–3 ASCII language letters, optional
  4-letter script and optional 2-letter/3-digit region (maximum 12 bytes). Variants,
  extensions/private-use tags and other parameters are unsupported; this is not
  full RFC 5646 support or locale-dependent rendering.
- Event ATTACH/IMAGE are omitted only when staging explicitly selects
  IgnoreByDeclaredProjection. No payload decoding, Base64 validation, storage or
  fetching occurs. Direct projected VALARM children are ignored notifications,
  never occurrences, title/timing input or wake scheduling. Other nested/projected
  components remain unsupported.
- Locally validated VTIMEZONE and STANDARD/DAYLIGHT definitions may accompany
  strictly UTC/DATE events. They are unused, not resolved or retained. Failed local
  definition checks still fail the source; no timezone engine or external fallback
  is introduced. Interleaved components are not event indices.

Explicit event end must match the start representation and be strictly later.
Missing timed end creates a point; missing DATE end adds one civil day. Checked
Gregorian conversion is independent of TZ/localtime/time_t. DATE remains a civil
range with exclusive end; original intervals are not clipped. Every event is checked
before window admission, including outside-window identities and unsupported fields.

Recurrence, overrides, DURATION, floating/TZID event times, event leap seconds,
unsupported scheduling, tentative/cancelled events, private/confidential presentation,
attendee semantics and unknown event/root extensions remain unsupported. METHOD
never relaxes required DTSTART. Duplicate UIDs fail regardless of SEQUENCE or dates;
no revision selection is implemented. Childless VCALENDAR remains rejected upstream;
non-event-only and multi-root sources are unsupported by this producer. Supported
outside-window-only sources can complete with zero records. These are initial
compatibility limits, not universal RFC behavior or full Google/Apple support.

Caller limits bound source validation, decoded text size and pairwise UID comparison
work. Two reusable caller-owned decoding buffers retain no per-event UID catalogue;
identical decoded identities fail even outside the window. Scratch must be disjoint
from the complete caller-declared live source-storage span (including any live parser/
ingestion storage), snapshot allocation and other live caller objects. The producer
checks spans and readable stage contents before writing scratch; callers still own
actual readable-span and lifetime guarantees. No allocation or capacity defaults
are introduced. Snapshot-owned titles and records survive release/poisoning of all
source scratch. Unexpected deferred bits fail closed; existing validator masks
remain unchanged. This does not select cross-source identity, timezone or recurrence
policy. See the [producer contract](docs/ENGINEERING.md#restricted-nonrecurring-ics-to-snapshot-producer)
and [host commands](AGENTS.md#commands-and-validation).

### GFX2 dashboard composition

[FiveDayDashboardRenderer](application/five_day_dashboard_renderer.h) composes one
read-only FiveDaySnapshot, current weather, short forecast, five-day forecast,
today's detailed events and battery indication. `render(gfx, snapshot, timeContext,
inputs, geometry)` returns scalar drawing/error and truncation counters. It owns
no display, data acquisition, snapshot transaction, allocation or refresh. Inputs
remain readable and immutable for the call; forecast pointers have explicit counts.

The [shared weather declarations](application/weather_values.h) are the exact POD
types formerly declared in weather.h. Production observation storage, successful
parsing, provider semantics and legacy drawing are unchanged; weather transport
now requires explicit CA/time trust. The renderer borrows those
records directly; it introduces no second weather or event catalogue. Battery input
is an already measured percentage plus validity; no ADC/calibration code is invoked.

`ee02PreviewGeometry()` supplies provisional retained 1600×1200 coordinates: calendar
x/y 10..1034; weather x1054/y10; short forecast y430; daily forecast x10/y1054;
today's events x1063/y970; battery body x1532/y1160. Dividers remain x1044,
left y1044 and right y570 (the actual production height/2−30 expression). Regions
and their separation are checked before drawing. The calendar delegates to the
unchanged FiveDayRenderer; today's bounded selection reuses its overlap/order/time
helpers, reads the same snapshot and shows original ranges, titles and source labels.
All-day entries precede timed entries; excess rows show +N more events.

Unavailable/invalid weather and forecasts show explicit unavailable indications.
Unknown condition codes have a generic icon/text fallback; supported day/night
categories use small vectors. Fixed-size weather text never requires a terminator;
invalid/unsupported characters are sanitized without changing input. Text shortening
and excess forecast entries are reported visibly. Fetch-time text is displayed as
supplied and freshness is explicitly **not assessed**. Forecasts use supplied order
and labels, without new date grouping or retention policy. Valid battery values are
clamped to 0–100%; invalid measurements show N/A without fabricated charge.

The deterministic preview below now composes this whole dashboard from its real
synthetic ICS pipeline plus synthetic weather/battery observations before the single
workspace-managed refresh. Its historical month-grid area is blank except for the
EXAMPLE DATA notice; monthly-grid retention and final agenda/grid/layout remain Open.
The canonical root now composes these panels from acquisition outputs. Strict C++11 and
ASan/UBSan host traces exercise panel bounds and immutable inputs, including a real
ICS ingestion/staging/producer-to-dashboard case. Target compilation, physical EE02,
runtime headroom and power checks remain owner-deferred and unverified. The canonical root uses this composition with checked timer sleep. Final RTC,
configuration/recovery and hardware validation remain incomplete.
See the [composition contract](docs/ENGINEERING.md#read-only-gfx2-dashboard-composition)
and [host commands](AGENTS.md#commands-and-validation).

### Production observation interfaces (graphics-free)

[weather_acquisition.h](application/weather_acquisition.h) exposes the existing
`fetchWeatherData(trust, request)`, `fetchForecastData(trust, request)` and
`fetchDailyForecastData(trust, request)` independently of TFT_eSPI/EPaper/GFX2 or
private credentials. `WeatherHttps::Request` borrows location/key byte spans with
explicit lengths excluding NUL; neither span is retained. Location is percent-encoded
(except unreserved characters and the existing comma separator), controls/NUL are
rejected and keys must be nonempty unreserved ASCII. Encoded location must fit the
existing 128-byte buffer and the complete URL the existing 256-byte buffer; exhaustion
rejects before network activity instead of truncating. The original hostname and
production example paths/queries remain unchanged. Inactive reference `weather.h` retains its old one-argument fetch wrappers; they
are not included or called by the canonical root. Weather-only counts and weekday
labels now live with the shared PODs, so active acquisition imports no legacy defaults. The guarded header owns the same sketch-translation-unit static
`weatherData`, `forecastData[FORECAST_COUNT]` and
`dailyForecastData[DAILY_FCST_COUNT]`, using the shared PODs. It creates no second
store, performs no acquisition at initialization, and does not change request order,
JSON interpretation, grouping, time labeling or failure retention.
The root calls the explicit-input operations directly on its one Wi-Fi session.
Legacy drawing/updateWeather orchestration remains inactive reference material.

Weather requests now require `WeatherHttps::Trust { clockTrusted, caPem, caBytes }`.
The caller guarantees the complete CA span is readable and stable through the call;
its length includes the sole terminal NUL. Untrusted time, null/empty material,
address overflow, embedded NUL or missing termination reject before network activity.
Each fresh secure client receives `setCACert()`, uses the unchanged HTTPS hostname
`api.openweathermap.org` through HTTPClient's host connection path, and explicitly
disables redirects. Non-200/redirect, initialization, connection, transfer and parsing
failures return false without insecure retry or HTTP fallback. Transfer errors are
checked before JSON decoding, and resources close before returning. The original
successful JSON interpretation/grouping and single observation store are preserved.

The root uses the promoted fresh-session SNTP gate described above, once per wake.
The retained legacy_time_sync/weather/calendar headers are inactive; in particular,
their insecure Apps Script path is neither called nor used as a fallback. All active
weather/calendar HTTPS requires CA-chain and original-hostname verification. Core
Debug None remains enforced to avoid platform request/query logging; this does not
prove every independently configured platform diagnostic safe. Final RTC trust and
certificate-update policy remain Open, and ordinary SNTP is not authenticated time.

[battery_measurement.h](application/battery_measurement.h) exposes the unchanged
`readBatteryMillivolts()` and `voltageToPercent()` without graphics: A5 enable, 5 ms
settling, A0 sampling, disable, original divider/scaling and discharge curve.
Inactive `battery.h` retains its historical drawing/logging code. The active root
calls only the shared measurement function, exactly once per admitted refresh.
Measurement does not supply a validity flag: a zero reading must not automatically
become a valid 0% dashboard observation; a valid low measurement can legitimately
map to 0%. The caller supplies that distinction through the existing dashboard input.
The dashboard can borrow the production weather arrays directly and accept an
already measured battery percentage/validity; it does not acquire or copy observations.

Focused strict C++11 and ASan/UBSan checks cover battery math/GPIO order and direct
shared-observation dashboard rendering. A graphics-free compile check uses small
Arduino/HTTP/JSON declaration stand-ins; source comparison checks unchanged parsing
and grouping, not a simulated successful JSON parser. These checks do not verify live
weather, cryptographic verification, ADC calibration or target compatibility.
The canonical root composes these inputs and checked timer sleep; target/hardware checks are
owner-deferred. Executable commands are in [AGENTS.md](AGENTS.md#commands-and-validation).

### Five-day snapshot renderer and EE02 example preview

[FiveDayRenderer](application/five_day_renderer.h) renders the existing read-only
snapshot into a caller-specified rectangle using Seeed_GFX. The provisional preview
is a five-column agenda with consecutive day/date headers, all-day entries first,
then timed entries in original UTC start order (stable input-order ties). Repeated
minimum selection scans the snapshot without an ordering array or event catalogue.
Overlapping entries have separate rows. Multiday entries repeat their original
range and show Continued on later days; all-day end dates are explicitly exclusive.
Points, window-crossing intervals and every hour of each supplied day are included.
This selects neither the final agenda/grid design nor waking hours.

Timed endpoints require an explicit conversion callback for the same timezone as
the snapshot's six day boundaries. Missing/inconsistent context fails before drawing.
The narrow [POSIX adapter](application/five_day_posix_time.h) checks an explicitly
configured TZ string and uses localtime_r; it does not resolve ICS VTIMEZONE. The
renderer verifies boundary dates/midnights, uses the supplied boundaries for overlap,
and prints numeric UTC offsets to distinguish repeated DST times. All-day civil
ranges never become UTC-midnight events.

Text uses inspected fixed 6×8 GLCD cells, scaled by the caller. Printable ASCII is
supported; each unsupported UTF-8 codepoint becomes one ? and controls become spaces.
Two title lines are bounded by stored text length; shortening shows ... without
changing snapshot text. Row exhaustion shows +N more events and reports omitted
**day entries**, without changing source completeness. A scoped absolute viewport
and cell bounds prevent drawing outside the region; caller viewport/origin is
restored. The renderer clears only its own rectangle, never the full display.

Every source outcome is inspected. Complete empty sources produce a legitimate
No events message; Incomplete/Pending sources produce warnings with bounded labels,
reasons and additional-source counts. Zero configured sources show No calendars
selected with all five date columns. With selected sources but no complete source,
the preview asks the
operator to check sources/retry instead of presenting an empty calendar.

The dedicated [preview sketch](previews/five_day_calendar/five_day_calendar.ino)
is outside production auto-compilation. It marks EXAMPLE DATA, explicitly configures
CET/CEST, builds and verifies the October 23–27, 2026 window from a fixed example
now (including the 25-hour DST day), and transactionally
feeds deterministic synthetic HTTP/ICS through IcsIngestion → IcsStructure →
IcsSourceStage → IcsNonrecurringSnapshot, producing timed, point, all-day, multiday
and cross-midnight examples alongside passive METHOD/X-WR metadata, administrative
fields, ignored TEXT/URL/attachment and a projected VALARM. A second ICS source with
unsupported cancellation
evidence becomes Incomplete. No occurrence records are inserted directly by the
sketch. Both sources finish inside one withScratch callback using checked, disjoint
stage/parser/ingestion/text slices; all borrowed users end before reuse. Its 32
records, 4 KiB text and 2 MiB workspace are demonstration arguments, not operating defaults. It poisons
scratch before Ee02PsramWorkspace::render(), composes the calendar and retained
panels with synthetic weather/battery values, checks render/refresh and shutdown
separately, and retains restricted workspace storage after uncertain shutdown.

The region is x/y 10 through 1034 inclusive (width/height 1025), inside the existing
x/y 1044 dividers in the 1600×1200 rotation. The dashboard clears/composes the
assigned screen, including the provisionally unassigned month area; the standalone
calendar renderer continues to clear only its region.
It remains independent of the canonical root acquisition/sleep cycle.
Compile with an **already available** documented toolchain, explicitly selecting the
vendored library; generated output stays outside the repository:

```sh
env ARDUINO_DIRECTORIES_DATA=/tmp/ee02-gfx2-compat.qWyDXC/data \
  /tmp/ee02-gfx2-compat.qWyDXC/bin/arduino-cli \
  --config-file /tmp/ee02-gfx2-compat.qWyDXC/arduino-cli.yaml \
  compile --clean --verbose --warnings all --jobs 2 \
  --fqbn esp32:esp32:XIAO_ESP32S3_Plus:PSRAM=opi,CDCOnBoot=default,PartitionScheme=default_8MB \
  --library /workspace/xiao-ee02-e-paper-calendar/third_party/Seeed_GFX2 \
  --build-path /tmp/ee02-calendar-preview-build \
  /workspace/xiao-ee02-e-paper-calendar/previews/five_day_calendar \
  > /tmp/ee02-calendar-preview-compile.log 2>&1
```

**Historical renderer-only preview target attempt: unavailable.** The temporary CLI path
no longer exists and no Arduino CLI is on PATH in this environment. No dependencies
were installed; no target board/library resolution or renderer linkage is claimed.
Historical fixture compile evidence below applies only to that fixture. Host tests
exercise renderer decisions with graphics-call traces, not physical glyphs/pixels.
For the restricted producer and updated ICS preview, Arduino compilation and physical
EE02 testing are explicitly owner-deferred; neither was attempted. Device compatibility
is unverified. No physical EE02 was tested or flashed. See the [renderer contract and evidence](docs/ENGINEERING.md#five-day-snapshot-renderer-and-ee02-preview)
and [focused commands](AGENTS.md#commands-and-validation). Broader
ICS normalization, settings UI/RTC/recovery, hardware/runtime validation and final
presentation policy remain separate work.

The separate [compile/link fixture](tests/gfx2_baseline/gfx2_baseline.ino) selects
`Seeed_Product::Seeed_ePaper_13INCH3_C`, which the pinned catalog maps to EE02,
T133A01 and native 1200×1600 at four bits/pixel. It calls initialization, rotation,
six-color drawing, text, refresh and shutdown APIs for both owned and borrowed paths.
Borrowing uses official factory construction, an instance that outlives its external
facade, and explicit `PanelMode::Colorful`. It has no credentials or network
calls and is outside the production sketch's root/`src` compilation layout. The
vendor tree is also outside sketch `src`; pass it explicitly only to the smoke build.
Do not install competing global copies. The canonical root also requires explicit
vendored GFX2 resolution; isolated fixture evidence is not a root target build.

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
checks and a closed-profile semantic producer now exist below. General ICS event/source
semantics remain incomplete; this ingestion layer itself never accepts a source.

Only HTTP 200 with complete supported framing and explicit successful logical-line
finalization produces ingestion Complete. Empty framed bodies mean zero lines,
not a valid empty calendar. Staged line delivery is provisional; errors, truncation,
unsupported encodings, close-delimited bodies and resource exhaustion fail explicitly.
Redirects yield bounded untrusted Location handoff data; they are never followed.
The separate [HTTPS acquisition API](#authenticated-streaming-ics-https-acquisition)
provides authenticated single-request socket binding. Redirect following, origin/
credential-forwarding policy and trust/clock provisioning remain later work.

Sessions allocate nothing and must live inside one workspace `withScratch()` scope;
callback views must not escape (C++ cannot revoke aliases). `bodyBoundaryReached()`
only observes declared framing completion; `finish()` remains mandatory. Capacities/time
limits are arguments, with no product defaults. Positive processingMs retains total
elapsed enforcement; zero disables only that budget. Positive stalledMs, monotonic
clock checks and all byte/resource limits remain mandatory. Tests evaluate 8 MiB body/16 MiB received bytes, 4 KiB physical/16 KiB logical
lines and 120-second processing/10-second stalled-read budgets provisionally.
Strict C++11 and ASan/UBSan host checks passed; no new target compile, live provider
or physical EE02 evidence is claimed. The root now composes ingestion through the coordinator; Apps Script is inactive
reference material, and snapshot/graphics component interfaces remain unchanged. Consult the
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
or runtime hardware behavior. This standalone parser adds no transport, production
device integration or recurrence/timezone resolution. The separate HTTPS API composes
it with acquisition; the restricted producer consumes staged parser output for
transactional snapshot publication; structural success alone never publishes it. See the
[canonical contract](docs/ENGINEERING.md#bounded-ics-propertycomponent-parser)
and [host commands](AGENTS.md#commands-and-validation).

### Selective source-local ICS dependency staging

[IcsSourceStage](application/ics_source_stage.h) borrows checked component/property
tables and an append-only byte pool within one workspace `withScratch()` lifetime.
It owns no allocation. Complete selected event, recurrence, revision, cancellation,
detail and timezone values/encoded parameters survive transient parser callbacks.
Known identifiers use compact codes; unknown names remain complete. No full-feed
String/document, UID index, decoded duplicate, normalization or publication is
created by this staging layer.

Declared non-event values are explicitly projected out. Unknown opaque components
already accepted by `IcsStructure` keep a descriptor/name and checked property
count, with no payload/property descriptors; staging alone does not invalidate events
for that presence. The restricted producer rejects such components as unsupported.
Relevant unknown properties/parameters remain bounded, copied and marked for review.
Exhaustion fails the source. Missing, original empty and omitted values are distinct.
`finish(ingestion, structure)` requires corresponding complete framing/structure and
matching counts. `StagedSuccessfully` means retention completed, not a semantically
complete schedule. The staging layer itself invokes no `FiveDaySnapshot` transaction;
the separate restricted producer owns supported-profile publication.

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

`selectTemporal` checks parameter selection without inspecting temporal payload
bytes. `decodeTemporal` and TemporalList separately validate complete scalar/list
values, including rejection of UTC DATE-TIME with TZID.

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
byte limits. This local API leaves RRULE and observance EXDATE semantics deferred; approved
TZNAME/COMMENT omissions are respected rather than decoded as empty values.

`LocalChecksPassed` always retains source-validation and transition-resolution
obligations; timezone identity, recurrence, extensions and other applicable work
remain pending. No child enumeration, TZID matching, external lookup, transition
calculation, event conversion, source acceptance or snapshot transaction occurs.
This timezone-local API does not perform the separate selected-property identity
matching described below.
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
exclusions, overrides, timezones and source completeness remain deferred.
IcsEventLocal, IcsTimezoneLocal and IcsSourceLocal do not invoke the grammar API. Obtain a
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

### VEVENT recurrence-property local coverage

[IcsEventRecurrenceLocal](application/ics_event_recurrence_local.h) inspects every
direct RRULE, RDATE, EXDATE and optional RECURRENCE-ID of one explicitly supplied
staged VEVENT. It requires accepted event-local checks and a complete bounded direct
property chain, then validates every rule with IcsRecurContext and exhausts every
date list through the accepted TemporalList completion contract. Caller limits bound
properties and aggregate RDATE/EXDATE member/value-plus-parameter bytes. The first
failure preserves its original diagnostic and clears all coverage counts/masks.

LocalCoveragePassed establishes local enumeration only. It always retains source
semantic validation; recurrence-set, expansion, missing start, parameter, list-context,
timezone, leap-second, PERIOD and override/RANGE obligations remain explicit where
applicable. Original contextual-rule bits stay in a separate mask. Lists are not
matched to DTSTART or one another; no deduplication, exclusion application, master
lookup or normalized occurrence is produced. Reports retain no staged aliases, and
all reads finish before release/workspace reuse. No observance EXDATE policy,
production behavior or snapshot operation changes. See the
[coverage contract and fresh evidence](docs/ENGINEERING.md#vevent-recurrence-property-local-coverage)
and [focused commands](AGENTS.md#commands-and-validation).

### Observance recurrence-local coverage

[IcsObservanceRecurrenceLocal](application/ics_observance_recurrence_local.h)
enumerates every direct RRULE, RDATE and EXDATE of one explicit staged STANDARD/
DAYLIGHT under its VTIMEZONE. It requires accepted observance-local validation,
then contextually validates every RRULE independently. Complete local core RRULE/
RDATE coverage reuses existing validators; there is no separate RDATE decoder.
For N RRULEs, the successful path invokes observance-local validation 1 + N times,
so RDATE checks may repeat internally. Reports count each property once, preserve
the two original deferred masks separately, and clear counts/masks on any failure.

EXDATE payloads remain uninterpreted with explicit EXDATE/extension review pending.
No recurrence-set reconciliation, expansion, transition generation, source acceptance
or snapshot publication occurs. Caller limits bound repeated work; final operating
budgets and ESP32 performance remain unmeasured, with no exactly-once guarantee.
Reports own no staged references; reads finish before release/workspace reuse.
See the [coverage contract and evidence](docs/ENGINEERING.md#observance-recurrence-local-coverage)
and [focused commands](AGENTS.md#commands-and-validation).

### Source-wide recurrence-local coverage

[IcsSourceRecurrenceLocal](application/ics_source_recurrence_local.h) composes one
complete IcsSourceLocal preflight, then visits every staged component in index order
across all VCALENDAR roots. It invokes IcsEventRecurrenceLocal for every VEVENT and
IcsObservanceRecurrenceLocal for every STANDARD/DAYLIGHT, using exact parent indices
and unchanged independent nested limits. All other component projections/review
obligations remain governed by the source preflight.

Failure ordering is two-phase: any source-local failure precedes recurrence dispatch,
even if an earlier component contains a recurrence error. After preflight success,
the first recurrence failure in component order wins. Original typed child Reports
are preserved; failures clear every aggregate mask/count. Success requires complete
enumeration and event/observance count agreement with preflight. Seven original
deferred domains stay separate, including unresolved observance EXDATE review.

LocalCoveragePassed is local coverage, never complete source acceptance, valid empty
schedule semantics, executable recurrence, resolved timezones or snapshot eligibility.
Bounded nested checks may repeat; no exactly-once guarantee, cache, allocation or
production integration is added. Pointer-free reports survive stage release; all
borrowed reads finish before scratch reuse. See the
[composition contract and evidence](docs/ENGINEERING.md#source-wide-recurrence-local-coverage)
and [focused commands](AGENTS.md#commands-and-validation).

### Selected TZID reference-local identity matching

[IcsTzidReferenceLocal](application/ics_tzid_reference_local.h) examines the TZID
parameter of one explicit staged VEVENT DTSTART, DTEND, RECURRENCE-ID, RDATE or
EXDATE. It verifies association and the complete event property chain, then uses
selectTemporal only. Absent and empty references remain distinct. For a nonempty
reference, it scans all staged components within the caller's budget and validates
every in-object VTIMEZONE locally before comparing its TZID TEXT identity.

Comparison uses exact decoded bytes through bounded cursors, after accepted RFC 6868
parameter/TEXT measurements. There is no decoded identity buffer, case folding,
Unicode normalization, external registry lookup or first-match choice. Reports
distinguish no local definition, one definition and multiple definitions, preserving
global-registry/definition-parameter review and timezone-resolution obligations.

Inspected and UniqueLocalDefinition are identity evidence only. The matcher does
not call decodeTemporal or TemporalList: even an invalid UTC/TZID temporal payload
may have a unique matching identity while its independent value decoder rejects it.
The selected-property matcher performs no automatic source enumeration; the separate
source-wide identity wrapper below composes it. No transition/UTC resolution,
recurrence execution, source acceptance or snapshot publication is added. Reports
own no staged aliases; matching indices are temporary source-local evidence, unusable as durable
references after release/reuse. See the [identity contract and evidence](docs/ENGINEERING.md#selected-tzid-reference-local-identity-matching)
and [focused commands](AGENTS.md#commands-and-validation).

### Source-wide VEVENT TZID identity coverage

[IcsSourceTzidLocal](application/ics_source_tzid_local.h) runs one complete
IcsSourceLocal preflight, then examines every direct VEVENT DTSTART, DTEND,
RECURRENCE-ID, RDATE and EXDATE across all staged VCALENDAR roots. Each occurrence,
including repeated lists and properties without TZID, is sent to the accepted
identity matcher with unchanged independent caller limits. Projected, opaque,
observance and RFC 9073 child payloads receive no identity dispatch.

The report counts NoReference, EmptyReference, NoLocalDefinition,
UniqueLocalDefinition and MultipleLocalDefinitions separately, and ORs the original
global-registry, definition-parameter and timezone-resolution pending flags. Missing,
empty and ambiguous identities remain observations; they do not reject the source.
Any source-local failure precedes identity dispatch. Thereafter the first property
budget or matcher failure in component/property order wins, preserving the complete
original child Report and clearing all aggregate coverage counts and flags.

LocalCoveragePassed establishes complete identity enumeration, not temporal-value
validity or source acceptance. The preflight independently checks selected scalar
fields; this wrapper adds no decodeTemporal or TemporalList calls. A malformed or
UTC/TZID RDATE/EXDATE member may still have a unique identity match while its separate
complete list decoder rejects it. Recurrence coverage remains a separate operation.
Repeated bounded definition scans are permitted without a cache or exactly-once
performance guarantee. Reports retain no staged aliases; all borrowed processing
finishes before scratch release/reuse. Device CPU/energy costs, timezone resolution,
missing/ambiguous-reference policy and snapshot eligibility remain unresolved. See
the [coverage contract and evidence](docs/ENGINEERING.md#source-wide-vevent-tzid-identity-coverage)
and [focused commands](AGENTS.md#commands-and-validation).

## Retained legacy reference and recovery limits

calendar.h, weather.h, battery.h, constants.h, driver.h, data_acquisition.h and
[google-calendar-api.gs](google-calendar-api.gs) remain inactive reference material.
They are outside the root include graph. The old insecure redirect-following Apps
Script request is not an active security fallback or a required deployment. No legacy
Google URL/API key or six-event store is used by the canonical application.

No root target build or validated physical recovery runbook is claimed. Device-hosted
settings webpage, recovery AP, three-failure handling, RTC validity/daily resynchronization,
provider expansion and final monthly-grid/layout decisions remain unimplemented/Open
as applicable. Root snapshot/weather data are volatile; no offline cache or retained
weather freshness policy is supplied. See the engineering anchor for current software
contracts, historical evidence and remaining hardware validation.

## License

See [LICENSE](LICENSE).
