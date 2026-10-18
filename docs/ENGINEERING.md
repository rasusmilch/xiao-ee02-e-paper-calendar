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

**Selected target; library borrowing, isolated workspace, normalized snapshot and
standalone HTTP framing/line unfolding, property/component parsing, selective
source-local dependency staging, typed primitive decoding, VEVENT-local checks and
timezone/observance-local checks, source-local coverage, standalone core RECUR
grammar, contextual RRULE validation and VEVENT recurrence-property coverage implemented;
production integration and hardware validation pending.** The owner-selected architecture
uses Seeed_GFX2 with a locally maintained external-framebuffer ownership extension,
one application-owned PSRAM arena, and a separate fixed-capacity five-day occurrence
snapshot in PSRAM. Both allocations are volatile across deep sleep. Current firmware
still uses the global TFT_eSPI/EPaper object and ordinary event/weather storage;
it uses none of the GFX2 borrowing extension, arena, normalized snapshot or streaming ICS pipeline.
Contradictory engineering evidence must trigger explicit review and adjudication,
not silent architectural substitution. Live transport and complete component/source
semantic validation remain unimplemented; detailed parser limits remain Open.

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

The approved [vendored source baseline](#vendored-gfx2-dependency-baseline) now
provides locally inspected GFX2 source. Its T133A01 path uses CPU-mediated
indexed-color conversion and SPI sends. Source-integration approval does not
establish a firmware-compatible dependency pin or runtime validation. Direct PSRAM
DMA is neither required by the selection nor established as supported. Verify exact
core/board configuration propagation, packaging, transfer/alignment requirements
and physical EE02 operation before claiming compatibility; do not substitute
arbitrary versions. The local [borrowing implementation](#local-t133a01-framebuffer-borrowing)
extends the imported baseline; production does not yet use it.

#### Provisional evaluation budgets

| Item | Initial evaluation allowance; not a validated operating limit |
| --- | --- |
| Shared PSRAM arena | **2 MiB (2,097,152 bytes)** for source staging, then the 960,000-byte framebuffer prefix |
| Independent five-day snapshot | **1,024 occurrences × estimated 320 bytes = 327,680 bytes (320 KiB)** |

The occurrence count, snapshot capacity and arena size are provisional evaluation
budgets. The historical 320-byte per-occurrence allowance is not an implemented
`sizeof`: the [normalized snapshot](#normalized-five-day-occurrence-snapshot) now
uses 24-byte records and a separately bounded text pool in the same allocation.
Final capacities remain unselected. Illustrative additional allowances in the supplied reference are
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
limits and recurrence-candidate/CPU budgets need separate adjudication. Snapshot
record/text exhaustion now aborts the source transaction as Incomplete; final
operating capacities remain Open. Enforce explicit limits and name incomplete sources; never
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

### Vendored GFX2 dependency baseline

**Pinned import with local panel-ownership changes, isolated from production.**
[third_party/Seeed_GFX2](../third_party/Seeed_GFX2/library.properties) contains the
approved initial source pin from `https://github.com/Seeed-Studio/Seeed_GFX2`, commit
`8f74b7aa91c4fcbc5ba2467bab4de4e7ce0eeeeb`. It was retrieved into a disposable
external Git directory with an exact-SHA, depth-one, no-tags fetch; `FETCH_HEAD`
and commit type were verified. At import, all **277 files (2,795,613 bytes)**,
including staged contents, matched upstream Git blobs byte-for-byte. Only
`src/panel/Panel_EPaper.h` and `.cpp` are subsequently locally modified for the
[borrowing contract](#local-t133a01-framebuffer-borrowing); all other vendor files,
metadata and licensing retain their imported bytes. Upstream
`library.properties` declares version 1.0.0 and the public header `Seeed_GFX.h`;
the immutable SHA and committed source, rather than the version string, establish
this reproducible source baseline. There is one maintained vendor tree, no submodule,
patch application system or second dependency lock.

The import includes the complete 275-file `src` hierarchy plus `library.properties`
and upstream [LICENSE](../third_party/Seeed_GFX2/LICENSE). Seeed code is MIT licensed;
the unchanged license also lists third-party notices, including Adafruit GFX fonts
and TomThumb. The [font license](../third_party/Seeed_GFX2/src/font/GFXFF/license.txt)
and inline notices remain intact. License references to upstream example PNG decoders
and tools do not mean those components were imported: examples, tools, assets, docs,
release/generated artifacts and Git metadata are excluded. Local include inspection
found no relative includes leaving `src`; platform headers such as Arduino, SPI,
Wire, FS and ESP32 SDK/PSRAM headers still require the compatible core. The complete
source includes upstream generic graphics/dither/UI functionality, not restored
application artwork orchestration.

The [isolated smoke sketch](../tests/gfx2_baseline/gfx2_baseline.ino) uses official
`Seeed_Product::Seeed_ePaper_13INCH3_C` selection. In
[ProductCatalog.cpp](../third_party/Seeed_GFX2/src/runtime/ProductCatalog.cpp), this
maps to `Board_XIAO_ePaper_EE02`, `Driver_T133A01`, `Panel_EPaper`, 1200×1600 at
four bits/pixel and colorful mode. The catalog gates XIAO products using board
macros including `ARDUINO_XIAO_ESP32S3_PLUS`; the EE02 SPI configuration has an
ESP32-S3-specific path and dual chip select. These are inspected source definitions,
not electrical evidence. The fixture checks those target macros and exercises both
ordinary product initialization and factory construction followed by borrowed-panel
initialization, colorful configuration, rotation, six-color primitives, text,
refresh and end. It also exercises the isolated application workspace and synthetic
snapshot, with no private credentials, networking or production application code.

Arduino must discover this library through the CLI's single-library
`--library <repository>/third_party/Seeed_GFX2` option for the isolated sketch.
The [README command](../README.md#isolated-gfx2-source-baseline-and-smoke-check)
records the exact isolated XIAO ESP32-S3 Plus configuration and current evidence.
At source import, compilation was NOT RUN because matching tooling was unavailable.
A subsequent disposable experiment compiled/linked the unmodified baseline using
Arduino CLI 1.5.1 and Arduino-ESP32 3.3.12 with OPI PSRAM; it did not validate hardware.
Existing production alpha-core/menu inputs remain unverified. With matching tooling,
inspect verbose library/include/compiler paths and linked graphics symbols; confirm
the vendored implementation and intended EE02/T133A01 configuration across library
translation units and reject unintended GFX2 or legacy TFT_eSPI selection. Do not
pass the new library to the existing production build or install a competing global
copy. Both the vendor and smoke sketch are outside the production root/`src` sketch
compilation layout; production includes, global EPaper, rendering and sleep are
unchanged. Isolated compile evidence does not establish a complete production build.

**Imported ownership boundaries and subsequent local scope:**

- `Seeed_GFX(product)` stores a pending product; `begin()` creates board/bus/driver/
  panel through the catalog, then begins the panel and configures colorful mode.
- [Panel_EPaper.cpp](../third_party/Seeed_GFX2/src/panel/Panel_EPaper.cpp) initializes
  buffer pointers to null. `begin()` initializes hardware, reads native geometry/
  driver depth and ordinarily allocates the framebuffer. ESP32 owned allocation first tries PSRAM,
  then falls back to `malloc`; that fallback is existing library behavior, not the
  selected application-arena allocation contract.
- Mode changes can free/reallocate buffers. Colorful configuration reuses an already
  allocated four-bit buffer; destruction calls `end()` and frees owned buffers.
  `DisplayInstance` resets by deleting the panel and other owned hardware. Raw
  `frameBuffer()` access alone does not register caller-owned memory. The local
  extension below intercepts allocation, mode changes and cleanup only in borrowed mode.
- Mirror paths may allocate full-screen temporary storage; the T133A01 driver reads
  packed nibbles, converts palette indices and writes through SPI for two chips.
  Calculated native framebuffer storage is 960,000 bytes. No allocation peak,
  direct PSRAM DMA capability, alignment guarantee or transfer completion is measured.

Keep production migration, the PSRAM arena, calendar parsing and five-day rendering
as separate authorized outcomes; the extension changes none of those implementations.

Source-integrity, license/inventory, protected-file, smoke-scope and local Markdown
checks cover this baseline. At import, full `git diff --cached --check` reported 1,438 inherited
vendor whitespace diagnostics; byte preservation takes precedence over upstream
style repair. Project-authored documentation and fixture whitespace are checked
separately. That import-stage whitespace/evidence record remains historical; it does
not describe whitespace introduced by the local extension. Physical EE02 validation
remains **NOT RUN**, necessary before runtime/display/memory compatibility claims.

#### Local T133A01 framebuffer borrowing

`Panel_EPaper::setBorrowedFrameBuffer(uint8_t* buffer, size_t availableBytes)`
returns `GfxResult` and registers writable caller storage **before `begin()`**.
Only native T133A01 1200×1600, indexed four-bit geometry is supported: 600 bytes/row,
exactly 960,000 admitted bytes, high nibble first. Rotation changes coordinates,
not storage size. Byte alignment is sufficient for this synchronous path; no DMA
alignment or PSRAM provenance/writability check is claimed. The caller guarantees
actual writable capacity, PSRAM placement when required, exclusive drawing access
and lifetime. Oversized arenas are permitted; their tail is never admitted or touched.

Null, undersized or address-overflowing spans and duplicate/active/already-owned
registration return `InvalidArgument`; unsupported driver/depth/geometry returns
`NotSupported`. Geometry is checked again after driver initialization, before any
caller memory is touched. First successful `begin()` aliases the registered slice
and fills exactly that slice with white (`0x00`); it never allocates, resizes, frees
or falls back to internal RAM. Repeated successful `begin()` is idempotent.
Registration before initialization does not expose a drawable framebuffer.

Use the existing `ProductCatalog::create(Seeed_ePaper_13INCH3_C, instance)` with a
separately owned `DisplayInstance`, register on its concrete `Panel_EPaper`, then
attach an external `Seeed_GFX` facade. The instance owns the board/bus/driver/panel
and must outlive the facade. The externally attached facade's `begin()` does not
configure product mode: explicitly call `panel->configure(PanelMode::Colorful)`
after initialization and before drawing/refresh. Initial colorful configuration
white-fills the same slice; repeated colorful configuration preserves its pixels.
Native/gray/BWRY configurations, their direct helpers and changed visible geometry
are rejected without replacing storage. Ordinary non-borrowing allocation/mirroring
behavior remains the imported behavior, including its PSRAM-to-`malloc` fallback.

Borrowed full refresh requires colorful mode and **no effective mirroring**.
Mirrored refresh is rejected with `NotSupported` before wake, padding or transfer,
so this path never needs a second full framebuffer. Partial/fast refresh is also
unsupported for this four-bit path. Existing T133A01 palette conversion, dual-chip
SPI commands and synchronous transfer implementation are unchanged. This proves
neither physical orientation nor direct PSRAM DMA support.

Check the concrete panel's refresh and `end()` results. Successful `end()` sleeps,
closes the bus/powers off through existing interfaces and forgets both caller aliases;
another cycle must register again, with no silent owned-allocation fallback. An
uninitialized pending borrow can also end without touching storage. Failed board,
bus or driver initialization retains registration for explicit `begin()` retry;
an initialization BUSY/transport error remains reported until that explicit retry.
After an active BUSY timeout or transport/shutdown failure, the first failure is
preserved; later begin/refresh/configure/wake/sleep/end cannot claim quiescence or
perform automatic recovery. An unsuccessful end keeps the active pointer/lifetime
obligation. No separate release API is needed for the selected processing → render
→ sleep sequence.

Destruction and `DisplayInstance::reset()` forget pointers and never free borrowed
storage, including on failure. They are **not proof of physical refresh completion
or safe panel shutdown**. The caller keeps storage alive through checked shutdown
and stack destruction; on uncertain hardware state, do not reassign/release it on
the strength of destruction. Check `Panel_EPaper::end()` directly: repeated end on
the unmodified facade/instance can mask an earlier error. Bus `end()`/board
`powerOff()` have void interfaces, so successful return does not independently
verify those electrical operations. The smoke fixture now uses the
[application workspace](#application-owned-psram-workspace) to retain allocation
on uncertain shutdown; that path provides no recovery or parser. The independently
allocated [snapshot](#normalized-five-day-occurrence-snapshot) is a separate owner.

Focused host tests compile the actual panel and `DisplayInstance` with test-local
GPIO/time shims and small bus/board/driver fakes. Linker allocation/free tracking,
byte-offset buffers, canaries and sanitizer checks cover registration, bounded
white-fill/pixels, owned allocation failure/regression, retry, rotation, mode and
mirror refusal, failure preservation, repeated lifecycle and reset/destruction.
See [AGENTS.md commands](../AGENTS.md#commands-and-validation) for invocation.
Host tests establish ownership software behavior, not T133A01 electrical behavior.
On 2026-10-12 these tests passed with strict C++11 warnings and separately with
AddressSanitizer/UBSan. The acquisition regression also passed its 16 combinations
and Wi-Fi failure. One clean verbose compile/link of the modified isolated fixture
passed with Arduino CLI 1.5.1, Arduino-ESP32 3.3.12, GCC 14.2.0 and the README's
XIAO ESP32-S3 Plus/OPI FQBN. Compiler paths and ELF symbols verified the vendored
library, board-provided macro, official EE02 factory/board, T133A01 and borrowed API,
without legacy TFT_eSPI linkage. Reported flash/static RAM is 506,915/24,720 bytes;
three inherited `Seeed_GFX.cpp` warnings remain (lines 795, 695, 707). Logs/build/ELF
are temporary external artifacts, not committed reproducibility inputs. Initial
host checking caught a masked pre-begin configuration error and an incorrect test
expectation about explicit mirror overrides; both were corrected before passing.
Hardware validation still requires actual allocation/largest-block/stack/heap
measurements, six colors/orientation, repeated initialization/update/sleep/wake and
failure behavior on physical EE02. Production rendering and memory remain unchanged.

### Application-owned PSRAM workspace

[application/psram_workspace.h](../application/psram_workspace.h) implements
`Ee02PsramWorkspace`, used only by the isolated smoke fixture. Production still
uses legacy `EPaper` and the six-event array; it invokes neither the isolated
ingestion layer nor the structural parser.
The independently allocated snapshot below is isolated from production.
The header is outside root/`src` auto-compilation, has no global allocation and
does not add GFX2 to the production sketch. This is the scoped application-memory
outcome, not production migration or validated memory capacity.

One noncopyable/nonmovable owner holds one allocation, capacity and the minimal
phase/error state. Plain allocation is insufficient for reusable checked access
and retention on uncertain shutdown; a general allocator, registry, persistent
state machine or production dependency-injection framework is unnecessary.

- `allocate(capacity)` has **no default capacity**. It requires at least 960,000
  bytes and a representable span (`PTRDIFF_MAX`/address overflow checks), rejects
  replacement and uses `ps_malloc()` only. The inspected core 3.3.12 implementation
  requests `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` and returns null without PSRAM;
  there is no internal-RAM fallback/realloc. Failure leaves `Unallocated` with
  zero capacity; invalid returned address spans are released before admission.
- `state()` and `capacity()` expose metadata, not an unrestricted allocation pointer.
  States are `Unallocated`, `Scratch`, temporary `ScratchAccess`, `Rendering` and
  `Retained`. No state/data is persisted or valid across deep sleep.
- `withScratch(offset, bytes, callback, context)` lends direct writable storage
  for a synchronous callback. Validate `offset <= capacity` and
  `bytes <= capacity - offset` before pointer arithmetic. Zero bytes at the end
  are allowed. Byte offsets have no DMA alignment restriction. Callers using
  typed structures must satisfy their alignment, construction and destruction
  requirements; this introduces no parser allocation tables or record schema.
  Direct access avoids compulsory parser copies. Reentrant scratch access,
  render, release and allocation are rejected while `ScratchAccess` is active.
- `render(draw, context)` accepts a drawing-only `Seeed_GFX&` callback and owns
  the complete scoped handoff. Finish all parser/network activity, temporary
  objects and scratch users first. Enter `Rendering` before factory construction;
  scratch access/reallocation/release remain disabled. Use the official
  `ProductCatalog::create(Seeed_ePaper_13INCH3_C, instance)`, register exactly
  **960,000 bytes**, attach an external facade, begin and explicitly configure
  `PanelMode::Colorful`, then draw and perform synchronous full refresh. Native
  prefix white-fill overwrites scratch; no graphics access admits the arena tail.
  No second full framebuffer is allocated. Effective mirroring is still refused
  by the unchanged borrowing interface.
- `RenderResult` preserves `operation` (factory/registration/begin/configure/draw
  or refresh) independently from `shutdown`, with `shutdownChecked` indicating
  whether concrete `Panel_EPaper::end()` ran. `ok()` requires both results and
  checked shutdown. A successful end cannot mask an earlier refresh failure.
  Drawing failure skips refresh but still checks shutdown after registration.
  The facade dies before `DisplayInstance::reset()`, while workspace storage stays
  alive throughout. This uses existing GFX2 interfaces without changing them.
- Factory/registration failure before admission permits ordinary cleanup. After
  registration, initialization/configuration or logical refresh rejection also
  permits reuse only after checked successful end and destruction of stack
  references. `Scratch` storage contents then remain unspecified; another render
  white-fills its prefix. Healthy repeated rendering reuses the same allocation.
- Any BUSY/communication operation failure or unsuccessful checked shutdown enters
  `Retained` and preserves the relevant failure. Scratch, render, allocation and
  release remain refused even after stack destruction. This restricts memory use,
  **not a recoverable software condition**: no retry, force-release, recovery or
  transition out of `Retained` exists. Destruction intentionally leaves retained
  storage allocated; it does not prove physical quiescence. A destroyed owner no
  longer exposes that allocation, so it remains an intentional unreclaimed block
  in the current runtime, not an offline/deep-sleep retention mechanism.
- `release()` frees only safely unborrowed `Scratch` storage and returns to
  `Unallocated`; repeated release is harmless. Destruction also frees healthy
  scratch storage and never frees active/retained storage.

All callbacks must return normally; no throw/longjmp, owner destruction, pointer
free/realloc, asynchronous work or concurrent access is permitted. Scratch aliases
must die before their callback returns; graphics callbacks must not retain facade,
panel or framebuffer references or change their lifecycle/ownership. The wrapper
checks mediated state/bounds and reentrancy; **C++ cannot revoke raw pointer escape,
prevent out-of-range pointer writes or make external aliases/races impossible**.
No stronger guarantee is claimed, and future parser use is not permanently
restricted to copy-only access. The separate snapshot copies its records/text;
no scratch aliases may survive handoff.
Any callback-provided `GfxResult::message` storage must remain valid for returned
results, including the retained owner's lifetime for hardware-class failures;
use stable messages, not pointers into temporary scratch/stack storage.

The smoke fixture supplies **2,097,152 bytes provisionally**, writes distinct
scratch patterns into prefix and tail, renders through this owner, then checks
the **1,137,152-byte tail** after healthy shutdown. Its original owned-buffer case
finishes/destructs before workspace allocation. No second framebuffer is allocated.
The fixture now also allocates the actual independent snapshot, stages a synthetic
title from scratch, commits it and checks it after prefix reuse. The 327,680-byte
snapshot allowance and final operating capacities remain provisional; the implemented
record/transaction contract is documented below.

Host [workspace tests](../tests/psram_workspace_test.cpp) compile this actual header
with actual `Panel_EPaper`/`DisplayInstance`. Minimal test-local PSRAM, factory,
facade and hardware stand-ins cover ownership, direct typed scratch use, bounds,
guards, phase refusal, white-fill/tail isolation, independent snapshot survival, logical errors, BUSY/transport
retention and repeated cleanup. They intentionally dispose the fake process heap
only after retention assertions; this is not a production release/recovery API.
The same suite now covers standalone ingestion inside guarded scratch slices,
with no new factory/rendering calls or snapshot publication in that case.
See [AGENTS.md](../AGENTS.md#commands-and-validation) for the exact command.
Strict C++11 and ASan/UBSan passed, as did the existing borrowing/acquisition
regressions. Host facade/factory stand-ins do not validate the actual renderer;
isolated target compilation must link the real vendored implementation.

Before the snapshot extension, on 2026-10-12 one clean verbose isolated compile/link
of the workspace-only fixture
passed using the existing Arduino CLI 1.5.1, Arduino-ESP32 3.3.12, GCC 14.2.0 and
the README's XIAO ESP32-S3 Plus/OPI configuration. Compiler paths and ELF symbols
confirmed actual workspace/official EE02/T133A01/borrowed-facade linkage, one
vendored GFX2 selection, board macro and OPI includes, without host shims or legacy
TFT_eSPI. Reported flash/static RAM is 507,771/24,720 bytes, not runtime headroom.
Three inherited `Seeed_GFX.cpp` warnings remain at 795, 695 and 707. Build/log/ELF
outputs are outside the repository under `/tmp`; no package installation occurred.
Initial host checking corrected a test ledger's handling of reused allocation
addresses; all final checks passed without changing vendored source.

Runtime capacity/headroom, PSRAM largest block and fragmentation, internal heap
minimum/free space, stack high-water and overlapping TLS/snapshot/workspace costs
remain unmeasured. Measure before/after allocation, scratch, graphics begin,
refresh/end and cleanup on exact hardware; 2 MiB is no operating guarantee.
Physical orientation/colors, refresh, electrical shutdown and repeated sleep/wake
remain owner-controlled validation. Software end uses void bus/power interfaces
and cannot independently certify those electrical operations.

### Normalized five-day occurrence snapshot

[application/five_day_snapshot.h](../application/five_day_snapshot.h) implements
`FiveDaySnapshot` independently from `Ee02PsramWorkspace`. Production still uses
the legacy six-event array; this snapshot accepts synthetic/already-normalized
inputs only. One noncopyable/nonmovable owner allocates the window, occurrence
slots, complete source catalogue and shared text pool in **one PSRAM-only block**.
`psramFound()` and the inspected core's `ps_malloc()` provide no internal-RAM
fallback. There are no per-event allocations, second event array, persistent cache,
provider identities, sorting representation or references into producer storage.
Fixed capacity means fixed for this initialization, not a permanent product limit.

#### Model, window and producer responsibility

- `Kind::Timed` stores original signed 64-bit UTC POSIX start/end seconds. Positive
  duration is a half-open interval, included when `start < windowEnd && end > windowStart`.
  Equal endpoints are a point, included when `windowStart <= start < windowEnd`.
  Negative duration is invalid. No clipping or waking-hour/elapsed-time filter applies.
- `Kind::AllDay` stores original signed 32-bit proleptic-Gregorian civil-day ordinals
  from 1970-01-01: inclusive start, exclusive end, with `end > start`. Inclusion uses
  the selected civil-day range. DATE-only events never become UTC-midnight intervals.
  Cross-midnight/multiday occurrences and those starting before the window survive
  whenever they overlap; merely touching either window boundary is excluded.
- `Window` copies the first selected civil-day ordinal and six supplied resolved UTC
  boundaries. `firstCivilDay + 5` must be representable and boundaries strictly
  increasing. No 86,400-second-day assumption or timezone resolver exists here.
  The producer must establish five consecutive valid civil dates in the **same
  selected display timezone** used for normalization and rendering. Unresolved,
  nonexistent/skipped dates require upstream failure, not invented boundaries or
  a complete schedule. This creates no second timezone configuration authority.
- Before `completeSource`, the producer must finish transport, expansion/exclusions,
  cancellations/moved instances, out-of-order exceptions, timezone normalization
  and occurrence-identity reconciliation. It must distinguish legitimate point
  occurrences from malformed explicitly supplied source intervals. The snapshot
  cannot verify these assertions or UID uniqueness; duplicate-looking records are
  retained within/across sources. Provider and normalizer implementations remain Open.

#### API, transactions and storage limits

- `initialize(window, recordCapacity, textCapacity, labels, sourceCount)` validates
  the complete catalogue and copies all nonempty, valid UTF-8 labels before publication.
  Every source starts `Pending`. Duplicate labels are valid; wake-cycle-local
  `uint16_t` indices distinguish up to 65,536 representable source slots, subject
  to allocation/layout capacity. This is a representation bound, not a selected quota.
  Records/text use checked 32-bit offsets/cursors, checked layout multiplication,
  natural alignment, `PTRDIFF_MAX` and address-span limits. Initialization failure
  leaves the owner unallocated; replacing a live allocation is rejected.
- `beginSource(index)` accepts one Pending source at a time. Published record/text
  cursors are append checkpoints; staged records remain inaccessible through
  `occurrence()`/`title()`. `addOccurrence(index, input)` validates range and text,
  returns `OutsideWindow` for valid excluded input, and copies admitted records/titles.
  Complete UTF-8 is required: reject NUL, invalid/overlong sequences, surrogates,
  invalid spans and exhausted capacity. Empty titles are valid; terminators count
  against text capacity. No truncation, interning or deduplication occurs.
- Any failed `addOccurrence` during an active transaction, including a misaddressed add,
  immediately restores both checkpoints and publishes that source
  `Incomplete` with an enum reason. Ignoring the error cannot lead to Complete.
  `failSource(index, reason)` explicitly cancels/fails the active source, even when
  record/text capacity is exhausted. Reasons are bounded provider-neutral enums,
  never URLs, credentials or raw provider payloads. Rollback logically discards
  staging; it is not a secure-erasure guarantee.
- `completeSource(index)` publishes the healthy source's full record range and
  `Complete` outcome at the single-threaded API boundary, including zero events.
  Each source has one terminal outcome; no reopening/replacement exists. Earlier
  committed records, titles and outcomes survive later failures. Pending is not
  Complete; Incomplete is not a successful empty calendar. Consumers can inspect
  every source to distinguish complete schedules from partial success.
- `window()`, `source()`, `sourceLabel()`, `occurrence()` and `title()` return bounded
  const views into this allocation, with occurrences in committed input order.
  No read/mutation concurrency is supported. Inputs must actually be readable;
  callers must not mutate borrowed const views or retain them beyond `release()`
  or destruction. `release()` refuses an active transaction, otherwise frees once
  and resets; repeated release/reinitialization is supported. Destruction ends all
  view/producer lifetimes. Nothing survives deep sleep or aliases the workspace.

Host and exact-target compilation verify `sizeof/alignof`: occurrence **24/8**,
source **16/4**, window **56/8** bytes. The candidate 1,024 records, 16 source slots
and 262,144 text bytes require **287,032 bytes** including window/layout (allocator
overhead excluded). With the candidate 2,097,152-byte workspace, requested payloads
total **2,384,184 bytes**. These are calculated/test arguments, not defaults,
measured PSRAM headroom or guarantees; the approximate 320 KiB allowance remains
provisional. Final capacities require representative workloads and hardware
largest-block/internal-heap/stack/TLS-overlap measurements and explicit review.

#### Executed validation and limitations

[Snapshot host tests](../tests/five_day_snapshot_test.cpp) passed strict C++11 and
ASan/UBSan: initialization/catalogue failures, allocation/address/layout bounds,
canaries, exact capacities, UTF-8/text ownership, half-open intervals/points,
all-day overlap, supplied 23/25-hour DST dates, hidden staging, complete/empty
publication, rollback/exhaustion, ignored-error refusal, source-index limits and
repeated cleanup. [Workspace integration tests](../tests/psram_workspace_test.cpp)
reuse actual `Panel_EPaper`/`DisplayInstance` with narrow hardware stand-ins:
commit a scratch-backed input, poison scratch, render/white-fill the borrowed
prefix and verify independent records/text and tail preservation. Exactly two
PSRAM allocations (snapshot/workspace) and no panel framebuffer allocation occur.
Existing acquisition and borrowing regressions also passed; commands are in AGENTS.

On 2026-10-12 one clean verbose isolated fixture compile/link passed using existing
Arduino CLI 1.5.1, Arduino-ESP32 3.3.12 and GCC 14.2.0 with XIAO ESP32-S3 Plus/OPI
PSRAM. The [README command](../README.md#isolated-gfx2-source-baseline-and-smoke-check)
records external build/log paths. Verbose paths and ELF symbols establish vendored
GFX2, actual snapshot/workspace, official EE02/T133A01 and borrowing linkage;
board macro/OPI paths and layout assertions passed, without host shims or legacy
TFT_eSPI. Reported flash/static RAM: **509,715/24,720 bytes**. The same three inherited
graphics warnings remain; vendor/workspace and production files are unchanged.
During development the test allocator's malloc linkage and a stale expectation
that misaddressed adds left a transaction healthy were corrected; final checks passed.

No physical execution, runtime allocation/headroom, timezone resolution, calendar
feed completeness, UID reconciliation, electrical shutdown or production firmware
build is established. The smoke fixture uses synthetic UTC boundaries and evaluates
snapshot/workspace coexistence only if later executed; compilation does not run it.
No ICS/provider/recurrence or three-consecutive-failures policy is implemented.

### Bounded streaming ICS ingestion

**Implemented standalone software; no live acquisition or calendar semantics.**
[application/ics_ingestion.h](../application/ics_ingestion.h) implements one bounded
raw HTTP response framer feeding the separate
[application/ics_logical_lines.h](../application/ics_logical_lines.h) byte unfolder.
Both are hardware-independent, noncopyable and allocation-free. The production
Apps Script/JSON path, `Ee02PsramWorkspace`, `FiveDaySnapshot`, GFX2 and display
lifecycle are unchanged. Lines are provisional until ingestion completes; even
Complete means supported framing/unfolding, **not complete, semantically valid ICS**.

#### Raw HTTP contract

- `IcsIngestion::initialize(scratch, capacity, limits, sink, sinkContext, clock,
  clockContext)` admits caller storage and explicit limits. `feed(bytes, count)`
  accepts raw HTTP/1.0 or HTTP/1.1 status/headers/body, not an HTTPClient-decoded
  body. Validate header tokens, strict CRLF, field counts/bytes and metadata length.
  Header/status bounds include CRLF and the terminating empty line.
- Ordinary ingestion requires status **200**. Content-Length must be decimal with
  checked overflow, no signs/junk/list values or duplicate fields (even identical).
  Consume exactly that number of bytes. HTTP/1.1 Transfer-Encoding accepts only
  one `chunked` coding; reject HTTP/1.0 TE, duplicate TE, CL+TE and coding chains.
  Absent Content-Encoding or single identity is supported; compression/unknown
  encodings and duplicate Content-Encoding fail. Informational/upgrade responses,
  204, 206, 304 and other non-supported statuses cannot complete ordinary ingestion.
  Reject unframed close-delimited bodies: ordinary closure cannot prove delivery.
- Chunk sizes use fully validated hexadecimal arithmetic. Validate bounded chunk
  extensions (token/quoted-string with permitted whitespace), ignore their values,
  consume each data block and its CRLF, then require a zero chunk **and** a complete
  bounded trailer section. Reject trailer fields changing framing, routing,
  authentication or payload processing; syntax-check other optional metadata.
  Ignoring a Digest trailer is not integrity verification. HTTP delimiters and
  trailers never become ICS content.
- Reaching a known body boundary leaves `InProgress`. `finish()` must also finalize
  the unfolder before returning `Complete`. `transport(Closed)` uses this same
  checked finalization; premature closure is `IncompleteFraming`. WouldBlock and
  `feed(nullptr, 0)` cannot complete acquisition. Submit only actual short-read
  counts, never requested counts. Observed bytes after the ordinary boundary are
  `UnexpectedBytes`, including positive feed after Complete. A future socket
  caller must check observed surplus and close its dedicated response connection;
  no detection of bytes arriving after that closure is claimed.
- `transport(Error/Cancelled)` gives explicit terminal failure. Status, HTTP status,
  received/decoded byte counts, line count and enum limit category are the only
  diagnostic metadata. The first failure remains latched through finish/release;
  subsequent calls do not emit more lines. Body bytes and header/URL contents are
  not copied into messages, logs or persistent results. Empty framed 200 bodies
  may complete with zero lines; this never means a valid empty calendar.

#### Redirect handoff and deferred security

301/302/303/307/308 with complete supported headers require one nonempty, bounded
Location. Reject duplicate/missing fields, whitespace/control/non-ASCII octets and
invalid percent escapes. `RedirectRequired` exposes a length-delimited,
**untrusted** Location view plus HTTP status. This is field-framing validation,
not complete URI validation or URL resolution. It is not NUL-terminated or safe
to log. Redirect bodies in the supplied read are discarded and never unfolded;
all supplied bytes still count toward the received-byte budget. A header handoff
does not assert complete acquisition of that redirect's body or final calendar.

No redirects are followed, resumed or chained here. No same-origin-only provider
restriction is selected: even an HTTP/cross-origin Location can be returned as
untrusted data. A separately approved transport adapter must resolve/validate
targets, bound redirect count and the whole acquisition duration/bytes, reject
HTTPS downgrade, apply origin/credential-forwarding policy, validate certificates
at every hop and own socket cleanup. Never inherit prototype `setInsecure()` or
automatic force-follow behavior. Authentication/provider coverage and trust/clock
provisioning remain Open. Synthetic tests establish none of these live guarantees.
This standalone supported subset establishes no permanent provider/transport restriction.

The locally inspected core 3.3.12 HTTPClient reads headers/chunk sizes into growing
Strings, loses duplicate recognized values, uses permissive numeric conversions
and stops its chunk writer at zero without trailer completion. `getStreamPtr()`
returns the underlying client, not dechunked body bytes. These source limitations
justify the small raw framer rather than another framing authority around that API;
no core patch, dependency upgrade or general networking framework is introduced.
The actual NetworkClientSecure read API also requires a later binding to distinguish
no available data from EOF/errors correctly; this unit consumes explicit events.

#### Unfolding and borrowed lifetime

`IcsLogicalLines::initialize(buffer, capacity, physicalLimit, logicalLimit, sink,
context)`, `feed()` and explicit `finish()` implement RFC 5545 section 3.1:
collect octets, require LF after CR, and hold a terminated candidate until a next
byte or confirmed body end. CRLF plus exactly one SPACE/HTAB continues the same
logical line; preserve all later whitespace and other octets, including UTF-8
split across reads/folds. Empty lines are delivered. Initial whitespace without
a preceding CRLF remains content. Lone LF, CR followed by another byte, unfinished
folds and unterminated final content fail. The 75-octet folding recommendation is
not a mandatory 75-byte limit. No UTF-8/property/value/component validation occurs.
Oversized lines fail, never truncate or silently skip; property-aware skipping requires a separately reviewed earlier-stream interface
and supported ignore policy. The structural parser cannot classify a line already
rejected by the unfolder.

Construct/destroy the session within **one `withScratch()` callback**. Partition
the admitted slice into metadata, Location and logical-line buffers using checked
subtraction, span/address validation and byte alignment. Inputs must be readable
and disjoint from this scratch; a receive slice can live beside it in the same
workspace. No per-line allocation, whole feed, second memory owner, event array,
framebuffer access or persistence exists. Callback views are const octet spans,
not strings, valid only during the synchronous sink call. Location views last
until release/destruction; callers must use neither after workspace reuse.

`release()` forgets every scratch/sink/clock binding, cancels unfinished ingestion
and preserves terminal outcomes/counters. It does not free caller storage. Sessions
are single-use; construct a fresh object for another response. Finish/release and
all users must return normally before scratch reuse; no throws, longjmp, owner
destruction, reentrancy or concurrent calls are supported. Mediated reentrancy is
rejected. C++ cannot prevent a consumer from escaping a pointer or violating bounds.

#### Limits and executed validation

All limits are caller parameters, with **no permanent defaults**. The host fixture
uses these reviewed evaluation candidates only:

| Parameter | Provisional test argument |
| --- | --- |
| Received / decoded body bytes, one response | 16 MiB / 8 MiB |
| Status+headers / fields | 16 KiB / 64 |
| HTTP metadata line / chunk metadata line | 2 KiB / 256 bytes |
| Trailer section / fields | 8 KiB / 32 |
| Location | 2 KiB |
| Physical / logical ICS line | 4 KiB / 16 KiB |
| Processing / stalled reads | 120,000 / 10,000 ms |

Physical limits exclude CRLF but count the removed continuation marker. Logical
limits count unfolded content. The three parser buffers total **20 KiB** for this
profile; a separate caller receive slice of 1 KiB gives **21 KiB**, not another
allocation. Received bytes include HTTP metadata and discarded redirect bytes.
Check numeric conversion/counters before increment/access; exact byte limits are
accepted and overflow fails. Monotonic clock callbacks check duration at feed,
periodic parsing and sink/finalization boundaries; backwards clocks fail. Stall
checks occur on feed/WouldBlock entry, and read-wait time starts after processing
the last nonempty feed. Zero reads never reset it. Exact time budgets are allowed;
exceeding them is ResourceLimit with ProcessingTime/StallTime. No callback preemption
or hard real-time guarantee exists. Caller polling and prompt callbacks are required.
Final capacities and physical PSRAM/internal-heap/stack/TLS headroom remain unmeasured.

Strict C++11 and ASan/UBSan [host tests](../tests/ics_ingestion_test.cpp) passed
1,325 partition/behavior scenarios, including every single split of representative
responses, one-byte and mixed short reads, CRLF/folds/UTF-8 across HTTP chunks,
length/chunk/trailer completion, malformed/unsupported profiles, redirect handoffs,
limits, fake-clock failures, callbacks and lifecycle cleanup. The preallocated
test sink and guarded buffers audit zero ingestion `new` allocations; source
inspection establishes no allocation calls. The first host run corrected a fixture
that accidentally included the C-string terminator in a binary input's byte count.
No implementation workaround or relaxed line rule was needed.

The workspace suite additionally passed strict and sanitizer checks with the
actual new header inside guarded scratch, a disjoint receive slice, release,
poison/reuse and repeated construction. That case adds no factory/graphics/snapshot
calls or extra allocation. Existing acquisition, snapshot and framebuffer regressions
passed unchanged. Exact commands are in AGENTS. Full production compilation,
new target ingestion compile/link, TLS/providers, physical EE02 and measured runtime
memory behavior were **NOT RUN**; the historical GFX2 fixture build does not validate
this code. Later semantic normalization must validate calendar completeness and
identity before independently authorized transactional snapshot publication.

### Bounded ICS property/component parser

**Implemented isolated syntax/structure; not a calendar importer.**
[application/ics_structure.h](../application/ics_structure.h) adds `IcsStructure`
without changing accepted ingestion, workspace, snapshot or vendor implementations.
Production remains on Apps Script JSON and legacy graphics. Selective source-local
staging, typed primitive decoding, selected VEVENT-local and timezone/observance-local
checks plus source-local coverage, standalone core RECUR grammar, contextual RRULE
validation and VEVENT recurrence-property coverage now
exist in isolation below; complete component/source semantic validation, recurrence
application, timezone resolution and snapshot producer integration remain unimplemented.

#### API, ownership and generic grammar

- `scratchBytes(limits, bytes)` returns the checked worst-case natural-aligned
  stack/name-storage requirement. `initialize(scratch, capacity, limits, sink,
  context)` borrows one disjoint caller slice. No allocation, fallback, recursion,
  whole-feed retention, general memory manager or persistence occurs.
- `consume(line, length)` accepts complete unfolded spans, not C strings or CRLF
  lines. It validates the whole line before emitting, including UTF-8 (no NUL,
  prohibited controls, overlong encodings, surrogates or invalid sequences).
  Names use the generic ASCII ALPHA/DIGIT/hyphen grammar and case-insensitive
  comparison; original property values and parameter spelling remain unchanged.
- Parameter names require equals; commas/semicolons/colons inside quoted values
  remain content. The first unquoted header colon begins the untouched value.
  Empty property and generic parameter values/list members are accepted; property-local
  VALUE/TZID checks now exist below; complete schema cardinality/meaning is later work.
  No truncation, Unicode normalization, generic
  TEXT unescaping or RFC 6868 decoding occurs. Iterators expose encoded spans and
  quoted flags, preserving recognized/unknown caret sequences and trailing carets.
- Synchronous BeginComponent/Property/EndComponent callbacks expose component
  kind/name, depth (root 1), source-local structural ordinal, parent ordinal and
  calendar ordinal. These numbers associate syntax nodes, not semantic UID identity.
  A participant/location/resource UID or SUMMARY remains attached to that child.
  All event/property/parameter views and iterators expire on callback return.
  Only copied bounded component names and structural state survive between lines.
- `lineSink` directly implements the existing boolean line callback. Parser status
  and bounded line/octet/limit diagnostics remain available when ingestion reports
  CallbackFailure. Errors retain no raw content, titles, URLs or credentials.
- Construct/destroy ingestion and structure inside **one `withScratch()` scope**,
  with checked, disjoint receive, ingestion and structure slices. Typed state uses
  placement construction/natural alignment inside the admitted scratch. Callbacks
  must return promptly/normally; no throws, reentrancy, concurrent use, escaped
  aliases or owner destruction. C++ cannot revoke consumer pointers or prove that
  a caller span is readable/writable.

#### Supported hierarchy and extension boundaries

| Parent | Recognized children |
| --- | --- |
| VCALENDAR | VEVENT, VTODO, VJOURNAL, VFREEBUSY, VTIMEZONE; unknown opaque components |
| VEVENT / VTODO | VALARM, PARTICIPANT, VLOCATION, VRESOURCE |
| VJOURNAL / VFREEBUSY | PARTICIPANT, VLOCATION, VRESOURCE |
| PARTICIPANT | VLOCATION, VRESOURCE |
| VTIMEZONE | STANDARD, DAYLIGHT |
| VALARM / STANDARD / DAYLIGHT / VLOCATION / VRESOURCE | No recognized children |

This combines RFC 5545 with RFC 9073 sections 4 and 7.1–7.3. Enforce property-before-
child placement and the specified child groups: alarms, participants, locations,
resources (omit unsupported groups by parent). PARTICIPANT uses locations then
resources. Repeated children are allowed within limits. VTIMEZONE follows its
own interleavable property/observance grammar and requires an observance child.

BEGIN/END have no parameters, require valid component tokens and exact
case-insensitive matching. Known prohibited nesting/order yields InvalidNesting.
Unknown names directly under VCALENDAR are delivered opaquely with a nonempty
bounded generic body; nested unknown opaque components are supported. No registry
membership or semantic support is claimed. A recognized child inside an opaque
context, or unknown child in another unsupported placement, yields UnsupportedProfile.
Nested VCALENDAR is always invalid. Recognized RFC 7953 VAVAILABILITY/AVAILABLE
also yield UnsupportedProfile, without claiming standards-valid availability is
malformed or implementing its semantics. No subtrees are silently discarded.

Every root requires one VERSION (2.0 or equivalent 2.0;2.0), one PRODID and a
component. Duplicate VERSION/PRODID/METHOD/CALSCALE or missing required root data
is InvalidMetadata; other versions are UnsupportedProfile. Property-value-specific
conformance and PRODID uniqueness are not established. Zero VEVENTs can be valid
structure; metadata-only roots or an empty response do not succeed.

#### Finalization, errors and evaluation limits

Root closure remains InProgress to permit another bounded calendar object.
Only explicit `finish()` after ingestion Complete may return StructurallyComplete;
open frames give IncompleteStructure and no root gives MissingRoot. Malformed
properties/parameters/text, invalid metadata/nesting, unsupported profiles,
exhaustion, cancellation, consumer refusal and invalid lifecycle are terminal.
The first failure survives later finish/cancel/release. New input after successful
finalization is invalid. `release()` cancels unfinished work, destroys structural
frames and clears all scratch/callback bindings without freeing caller storage;
sessions are single-use, with fresh construction for another source.

Structural success proves the declared generic text/hierarchy profile only. It is
not ingestion success, full property/extension semantics, a complete event schedule,
normalized occurrences or FiveDaySnapshot Complete. A later producer must finish
transport, semantic validation, identity/recurrence/timezone reconciliation and
normalization before transactional publication. Unsupported potentially relevant
semantics must not be silently dropped while claiming complete calendar results.

| Explicit caller limit | Provisional host evaluation argument |
| --- | --- |
| Logical line | 16 KiB, alongside unchanged 4 KiB physical/8 MiB body ingestion candidates |
| Parameters / values per parameter / total values per property | 32 / 64 / 128 |
| Depth / component-name bytes | 8 / 64 |
| Properties per component / source | 4,096 / 65,536 |
| Components / calendar objects per source | 4,096 / 4 |

All limits have no defaults. Check sizing, address spans, offsets and counters;
exact limits are admitted, overflows fail. The host evaluation requires **839
scratch bytes** (8 actual 40-byte frames, 512 name bytes, up to 7 alignment bytes);
`sizeof(IcsStructure)` is **208 host bytes**. These are layout arithmetic, not
measured stack/PSRAM headroom or selected product capacities. Work is bounded by
line scans, iterator traversal and counters; callbacks are not preempted. Current
oversized-line handling stays fail-closed upstream. Final capacities, semantic extensions/provider policy and hardware memory limits
remain Open; the implemented selective staging schema is documented below.

Strict C++11 and ASan/UBSan [tests](../tests/ics_structure_test.cpp) passed **565**
property/hierarchy/fragmentation scenarios, auditing zero parser `new` allocations.
The actual-workspace regression additionally checks disjoint guarded slices,
one-byte raw HTTP delivery, child identity, repeated release/poison/reuse and no
additional allocation/factory/framebuffer. Existing ingestion, acquisition,
snapshot and framebuffer regressions passed. Test-only copied records are not
production staging; no new parser path invokes snapshot transactions.

The extended isolated [EE02 fixture](../tests/gfx2_baseline/gfx2_baseline.ino)
compiles/links synthetic parser use inside the existing workspace, with no parser-
driven snapshot calls. Existing owned/borrowed/synthetic snapshot cases remain
separate. The final command/toolchain evidence below is isolated, not a production
firmware build, live feed or runtime measurement. Arduino CLI **1.5.1**,
Arduino-ESP32 **3.3.12** and Xtensa GCC **14.2.0** compiled/linked successfully;
dependencies and ELF symbols confirm this parser, the XIAO Plus macro/OPI path,
one vendored GFX2 library and EE02/T133A01. No legacy TFT_eSPI was selected.
The compiler reports **513,467 bytes flash** and **24,720 bytes static globals**
(out of 3,342,336 / 327,680), not runtime PSRAM/internal-heap headroom. The verbose
log contains no warning/error diagnostics; CLI uses `-w`, so this is not a claim
of warning-free vendor source. Strict host compilation did not suppress warnings.

```sh
/tmp/ee02-gfx2-compat.qWyDXC/bin/arduino-cli \
  --config-file /tmp/ee02-gfx2-compat.qWyDXC/arduino-cli.yaml \
  compile --clean --verbose \
  --fqbn esp32:esp32:XIAO_ESP32S3_Plus:PSRAM=opi,CDCOnBoot=default,PartitionScheme=default_8MB \
  --library /workspace/xiao-ee02-e-paper-calendar/third_party/Seeed_GFX2 \
  --build-path /tmp/ee02-ics-structure-build tests/gfx2_baseline \
  > /tmp/ee02-ics-structure-compile.log 2>&1
```

No installation/upgrade, production build, live calendars/TLS, flashing, physical
EE02 refresh or runtime heap/PSRAM/stack measurements were performed. Host behavior
and compilation establish neither complete real-calendar semantics nor hardware
capacity/correctness. Prior source-import, borrowing, snapshot and ingestion
validation history remains distinct from this parser outcome.

### Selective source-local ICS dependency staging

**Implemented isolated retention; not a calendar importer or semantic producer.**
[application/ics_source_stage.h](../application/ics_source_stage.h) composes the
accepted ingestion/structure callbacks without changing their implementations.
It retains a declared VEVENT projection, not a complete source document. Production
Apps Script acquisition, graphics, workspace and independent snapshot are unchanged.

#### Storage, API and lifetime

`IcsSourceStage` is noncopyable/nonmovable and never allocates/frees memory.
`requiredBytes(limits, bytes)` checks multiplication, alignment, offset widths and
`PTRDIFF_MAX`; `initialize(storage, capacity, limits)` admits a readable/writable
caller slice after address-span checks. These checks cannot prove real PSRAM or
writability. The workspace supplies PSRAM without a new allocation owner/fallback.

One naturally aligned component table, property table and append-only byte pool
live within the admitted slice. Placement construction gives typed records their
C++11 lifetime. Host and isolated-target assertions establish component **48/8**
and property **40/4** sizeof/alignof. Component records retain full names, kind,
structural/parent/calendar ordinals, first/last property links, directly observed
property counts, flags and closure. Property records retain ownership, compact
recognized codes or complete original unknown names, offsets/lengths, parameter
count, next link, disposition and review flags. Recognized identifier spelling is
canonical/case-insensitive; values and encoded parameters are copied exactly once.
No full content lines, interning, compaction, UID index or decoded duplicates exist.

Construct/use/release/destroy all source-local users inside **one `withScratch()`
callback**, with disjoint receive/ingestion/structure/stage slices. Synchronous
`consume(event)`/static `sink` accepts only validated `IcsStructure` callbacks;
it is not another public property tokenizer. Check component association and input
spans before copying. Never feed hand-built malformed encoded parameter views to
its validated-span iterators. No reentrancy, concurrency, throws, pointer escape
or owner destruction is supported. C++ cannot revoke aliases or prevent raw writes.

`finish(ingestion, structure)` takes the corresponding source pipeline after both
explicit finalizers. Counts detect association errors; they do not authenticate
arbitrarily substituted objects. `cancel()` and `release()` preserve the first
terminal failure. Release cancels unfinished work, destroys records, clears every
storage binding and frees nothing. Sessions are single-use; construct a fresh stage
for the next source. Successful terminal status/counters may survive release, but
all readers fail after release. No source-local data survives deep sleep/reuse.

#### Selective retention and dispositions

Every non-opaque property has a descriptor, including omissions. The fixed
context-sensitive table is a retention profile, not universal semantic support.

| Component | Required retention | Conditional retention |
| --- | --- | --- |
| VEVENT | UID, DTSTART, DTEND, DURATION, RECURRENCE-ID, RRULE, every RDATE/EXDATE, STATUS, SEQUENCE, DTSTAMP, LAST-MODIFIED, SUMMARY | CREATED, DESCRIPTION, LOCATION, COMMENT, ORGANIZER, ATTENDEE, REQUEST-STATUS, RELATED-TO, TRANSP, CLASS, PRIORITY, CATEGORIES, CONTACT, RESOURCES, GEO, URL |
| VCALENDAR | VERSION, PRODID, METHOD, CALSCALE | UID, NAME, DESCRIPTION |
| VTIMEZONE | TZID, LAST-MODIFIED | TZURL |
| STANDARD/DAYLIGHT | DTSTART, TZOFFSETFROM, TZOFFSETTO, RRULE, every RDATE/EXDATE | None initially |
| Event-associated PARTICIPANT | UID | PARTICIPANT-TYPE, CALENDAR-ADDRESS, SUMMARY, DESCRIPTION, LOCATION, GEO, CREATED, DTSTAMP, LAST-MODIFIED, SEQUENCE, STATUS, PRIORITY, URL, CATEGORIES, COMMENT, CONTACT, RELATED-TO, RESOURCES, REQUEST-STATUS |
| Event-associated VLOCATION | UID | NAME, DESCRIPTION, GEO, LOCATION-TYPE |
| Event-associated VRESOURCE | UID | NAME, DESCRIPTION, GEO, RESOURCE-TYPE |

All repetitions and input order remain available. No DTSTART/window filtering,
revision choice, UID reconciliation, recurrence expansion or time conversion occurs.
Structural ordinals are not UIDs and are monotonic across calendar objects within
one source. Parent/calendar context prevents participant UID/SUMMARY contamination.
Never automatically merge equal UIDs across sources/objects. METHOD and scheduling
context remain raw; iTIP rules are not applied universally. A cancellation-only
message without a suitable baseline cannot establish a complete schedule.

`IgnoreByDeclaredProjection` stores encoded parameters and original value length,
with no value bytes. `value()` returns `ValueOmitted`, distinct from a retained
zero-length value and an absent descriptor. No empty original value is fabricated.
Declared omissions are:

- Context-specific RFC 5545 task/journal/free-busy payload fields; retain their
  designated UID/RECURRENCE-ID identities where applicable.
- Standard VALARM notification payload; never use it as enclosing event timing.
- Recognized RFC 9073 child payload under projected-out non-event parents, while
  retaining designated child UID identity.
- Observance TZNAME/COMMENT and calendar COLOR/SOURCE/REFRESH-INTERVAL values,
  outside this event projection. No external URI is followed.
- ATTACH without parameters, or ATTACH/IMAGE with exactly VALUE=URI, or exactly
  VALUE=BINARY plus ENCODING=BASE64, in declared standard scopes. Parameter order
  and validated quoting are supported; duplicates/additional/ambiguous parameters
  are ineligible. Other event/calendar forms retain complete values conditionally;
  standard non-event payload follows its separate contextual projection.

Unknown parameters on an omission candidate promote it to complete unreviewed
retention. Required/conditional properties retain complete parameters and review
flags. The implemented parameter recognition is deliberately conservative; the typed
layer below checks selected temporal parameters, while complete parameter/value
conformance remains later validation. Unknown properties, unexpected
context and known unselected relevant extensions (including STRUCTURED-DATA,
STYLED-DESCRIPTION and CONFERENCE) retain full values/parameters and unresolved flags.
No broad X-property ignore rule exists. Conditional detail fields remain intact
pending final presentation decisions. If a later selected feature needs omitted
content, revise the contract through review or report incomplete; do not invent it.

#### Opaque component projection and unknown-property distinction

RFC 5545 section 3.6 requires ignoring unrecognized x-comp/iana-comp values.
Accepted `Kind::Opaque` therefore gets one full-name component descriptor marked
`OpaqueProjectedOut`, its exact parent/calendar context, checked property count and
closure. Nested opaque components get their own records. Their property callbacks
create **no property descriptors or copied name/parameter/value bytes** and charge
no unresolved budget. `payload()` returns `PayloadProjectedOut`; empty property
links are not evidence that the original opaque component had no properties.

The parser still validates every logical line, count, depth and closure. Known
VEVENT inside opaque context, unknown unsupported placement and RFC 7953 availability
retain upstream UnsupportedProfile. Malformed nesting, UTF-8, incomplete framing
and oversized lines remain fatal. Do not bypass parsing or skip raw subtrees.
Opaque UID/SUMMARY/RRULE never becomes event data. Ignored-component presence alone
is not a blanket semantic failure; it also does not imply extension understanding.

By contrast, unknown properties within recognized VEVENT/timezone/observance and
relevant RFC 9073 contexts remain complete and `RetainUnreviewed`. Later semantic
processing must resolve relevant review flags or report unsupported/incomplete.

#### Completion, readers and failures

Normal const readers are available only for a bound `StagedSuccessfully` result:
`component`, `componentName`, `payload`, `property`, `propertyName`, `parameters`
and `value`. Values/parameters/unknown names reference stage-owned bytes; canonical
recognized names have program lifetime. Views/iterators must not outlive release,
destruction or workspace reuse. There is no snapshot or semantic publication API.

Finalize only when the corresponding ingestion is Complete, structure is
StructurallyComplete, all staged components are closed and component/calendar/
observed-property counts match. Require:

`observedProperties = storedProperties + ignoredOpaqueProperties`

Named standard omissions count as stored descriptors; opaque properties do not.
Successful staging proves the bounded retention contract only, not typed validity,
recurrence/timezone completeness, normalized occurrences or FiveDaySnapshot Complete.
Opaque-only structurally valid calendars can stage; they fabricate no VEVENT and
need later source/semantic validation before any successful empty publication.

Statuses distinguish invalid arguments/state/association, UnsupportedProfile,
ResourceFailure, UpstreamFailure and cancellation. Bounded diagnostics contain
ordinal/limit/disposition and upstream status codes, never payload/URLs/titles.
Stage resource failure propagates through structure ConsumerFailure and ingestion
CallbackFailure while preserving the specific stage cause. Later failures invalidate
all normal readers, including earlier closed components. Release is logical discard,
not secure erasure. The separate snapshot and earlier committed calendars are untouched.

#### Evaluation limits and executed evidence

All capacities are caller-specified with no permanent defaults. The reviewed
candidate is 4,096 component slots, 8,192 stored-property slots, 1,048,576 pool bytes,
256 review items and 65,536 review bytes **within the same pool**. A property with
unknown parameters charges one review item and its complete encoded parameter span;
a wholly unreviewed property charges all copied property bytes. Copying is never
doubled. Zero review budgets intentionally refuse unreviewed information.

This candidate requires **1,572,874 bytes** worst-case stage storage (48C + 40P +
pool + 10 alignment bytes). Existing 21,504 receive/ingestion bytes and 839 structural
bytes total **1,595,217**, leaving a calculated 501,935 in a candidate 2 MiB workspace.
This excludes control-object stack/TLS/runtime costs. Later recurrence/timezone
scratch remains unverified; even a hypothetical 256 KiB reserve proves no fit.
The independent snapshot is a separate allocation. Source bytes may exceed retained
storage when irrelevant payload is discarded; finite dependency storage cannot
represent arbitrarily large relevant feeds. Explicit failure is required.

Strict C++11 and ASan/UBSan stage tests passed **618** synthetic behavior/fragmentation
scenarios with zero audited `new` allocations during stage/ingestion operations.
Actual-workspace tests passed strict and sanitizer runs: guarded disjoint slices,
one-byte delivery, retained UID after parser/ingestion poisoning, opaque accounting,
release/poison/reuse over three cycles and no extra PSRAM owner/factory/framebuffer.
Existing structure/ingestion regressions passed unchanged. Commands are in AGENTS.
Early test development corrected strict formatting/type errors and a duplicate
fixture END line. A regression caught overly broad ALTREP recognition on TZNAME;
context-sensitive handling now retains that omitted-candidate value as unreviewed.
Final behavior checks pass without relaxing accepted interfaces.

The isolated fixture includes synthetic staging before the separate existing
snapshot cases, without stage-driven transactions. One clean verbose final-source compile/link passed on 2026-10-15 using existing
Arduino CLI 1.5.1, Arduino-ESP32 3.3.12 and Xtensa GCC 14.2.0. The board supplies
ARDUINO_XIAO_ESP32S3_PLUS and OPI paths; layout assertions pass. Verbose discovery
selects only vendored Seeed_GFX2 1.0.0 plus core SPI/Wire, with no legacy TFT_eSPI.
Dependency output and ELF symbols establish actual stage inclusion/linkage. Reported
flash/static globals are **523,495/24,720 bytes** (limits 3,342,336/327,680),
not measured runtime heap/PSRAM headroom. CLI compiler recipes suppress warnings with `-w`; absence of diagnostics does not
establish warning-free vendor source. Strict host checks do not suppress warnings.
The preliminary build also passed; final review then switched raw table traversal
to byte offsets for C++11 placement-object lifetimes and corrected/extended edge
fixtures, followed by final strict/sanitizer and target revalidation.

```sh
env ARDUINO_DIRECTORIES_DATA=/tmp/ee02-gfx2-compat.qWyDXC/data \
  /tmp/ee02-gfx2-compat.qWyDXC/bin/arduino-cli \
  --config-file /tmp/ee02-gfx2-compat.qWyDXC/arduino-cli.yaml \
  compile --clean --verbose \
  --fqbn esp32:esp32:XIAO_ESP32S3_Plus:PSRAM=opi,CDCOnBoot=default,PartitionScheme=default_8MB \
  --library /workspace/xiao-ee02-e-paper-calendar/third_party/Seeed_GFX2 \
  --build-path /tmp/ee02-ics-source-stage-build tests/gfx2_baseline \
  > /tmp/ee02-ics-source-stage-compile.log 2>&1
```

Build/config/cache outputs stay outside the checkout. No installation or upgrade
occurred. No live providers, production firmware build, physical EE02 execution,
measured headroom, recurrence or timezone correctness is established by host tests
or compilation.

### Typed ICS primitive values

**Implemented isolated representations and property-local checks; not complete
component/event/source validation.** [application/ics_typed_values.h](../application/ics_typed_values.h)
adds the `IcsTypedValues` namespace without changing accepted ingestion, structure,
stage, workspace, snapshot or graphics implementations. Production remains unchanged.

#### API, representation and context

Stateless `decodeDate`, `decodeDateTime`, `decodeDuration`, `decodePeriod`,
`decodeInteger` and `decodeUtcOffset` consume complete length-delimited spans and
return small copied POD fields. There is no allocation, recursion, decoded property
database, epoch/civil-ordinal conversion or source-completion state machine.
Temporal union members are activated by named trivial assignment from live local
decoded objects, respecting C++11 object lifetimes without heap allocation.

- DATE validates eight digits, Gregorian month/day and century leap rules across
  the four-digit 0000–9999 field representation, not a permanent product date range.
- DATE-TIME validates date plus HHMMSS with optional Z; RFC ABNF literal markers
  are case-insensitive. Reject numeric offsets, missing seconds, fractions and 24:00.
  UTC, floating and property-associated TZID modes remain distinct. Second 60 is
  preserved with `NeedsContext`; no leap-second confirmation or rewrite occurs.
- DURATION preserves sign, week/day/hour/minute/second magnitudes. Zero/negative
  primitive durations are not acceptance of their use in an event. The strict
  nested RFC grammar accepts PT1H0M2S and rejects PT1H2S; no compatibility relaxation
  is selected. Checked uint64 magnitude overflow is ResourceLimit. Nominal days/
  weeks are never converted to fixed elapsed seconds.
- PERIOD accepts DATE-TIME/end or DATE-TIME/positive duration, including local
  forms (RFC 5545 verified erratum 7332). Ordinary UTC endpoints must increase.
  Leap-second or unresolved local endpoint ordering returns NeedsContext; civil
  ordering around a timezone gap does not prove elapsed ordering. Positive-duration
  forms establish their representation without calculating a resolved interval.
- INTEGER enforces signed 32-bit RFC bounds; field-specific constraints remain
  later work. UTC-OFFSET requires sign/HHMM[SS], rejects negative zero/second 60,
  permits hours through 23 and returns signed seconds without transition arithmetic.

`Ok` establishes only the requested representation and implemented local checks.
`NeedsContext` carries parsed fields with a pending intrinsic/contextual condition.
Neither resolves local-time ambiguity, TZID dependencies or complete calendar data.
No device timezone substitution occurs. Calendar METHOD, revisions, recurrence,
timezone transitions and normalized snapshot publication remain deferred.

#### Stage access, parameters and temporal lists

`readProperty(stage,index,out)` uses actual property/component/name/parameter/value/
payload readers and supplies code, kind, disposition, review flags and structural/
parent/calendar identities. It distinguishes unavailable/omitted data from original
empty retained data. Stage terminal status survives release, so reader availability
is checked instead. No descriptor, byte, flag or counter is changed.

`selectTemporal`/`decodeTemporal` support event DTSTART/DTEND/RECURRENCE-ID,
RDATE/EXDATE, CREATED/DTSTAMP/LAST-MODIFIED and DURATION, plus relevant timezone/
observance fields. Defaults and permitted DATE/DATE-TIME/PERIOD alternatives follow
their property context. VALUE/TZID must be singular with one member; unsupported
VALUE tokens never fall back. An explicit default VALUE is accepted (RFC 5545
3.2.20); fixed-type timestamp/DURATION properties cannot override it with another
standard type. UTC revision stamps must be UTC. Observance DTSTART
must be local without TZID. Reject TZID on DATE/UTC; an empty present TZID stays
present and NeedsContext, never floating. Referenced definitions may appear later.

Known inapplicable standard parameters fail locally; extension parameters preserve
review obligations. RANGE on RECURRENCE-ID is retained for later interpretation.
No generic parameter or primitive acceptance establishes component-schema legality.
All RECUR access through the unchanged temporal selector returns UnsupportedFeature
(deferred integration), including ordinary RRULE and RSCALE/SKIP. A separate core
grammar API now exists below; the selector does not invoke it. This does not classify
standards-valid recurrence extensions as malformed or interpret them as Gregorian.

`TemporalList(stage,index,{members,bytes})` iterates RDATE/EXDATE with an explicit
value-plus-parameter byte budget and member limit. Empty/mixed-invalid members,
capacity exhaustion and unavailable stage reads fail explicitly. Members are
provisional: exhaust `next()` through End, then require `complete()` and final
Ok/NeedsContext. Before exhaustion status is InProgress. Errors are sticky.
There is no recurrence application, deduplication, sorting or window selection.
RDATE PERIOD representation success does not establish its semantic applicability.

#### TEXT, parameter output and lifetime

`measureText`/`writeText` implement scalar RFC 5545 TEXT escapes for backslash,
comma, semicolon and n/N newline. Raw colon/quotes and valid Unicode are preserved;
unknown/dangling escapes, escaped colon, unescaped scalar delimiters, invalid UTF-8,
NUL and prohibited controls fail. CATEGORIES/RESOURCES and structured properties
need later property-aware splitting; do not decode their whole lists as scalar TEXT.

`measureParameter`/`writeParameter` take validated encoded members from existing
iterators, excluding quotation delimiters. RFC 6868 ^n/^^/^' decode to LF/caret/
quote; unknown ^N/^z and trailing caret remain unchanged. Parameter backslashes
are not TEXT-decoded. Ordering, spelling and repeated source parameters stay intact.

Writers fully validate/measure before writing, use explicit optional NUL termination
and reject short destinations without partial output or truncation. Span/address
arithmetic and detectable input/output overlap are checked. Callers must supply
actually valid, disjoint storage, including no overlap with OTHER live stage/parser
data; existing interfaces cannot prove ownership or revoke escaped aliases.

Construct cursors and use all views inside the stage's one `withScratch()` lifetime.
The stage object must remain alive; borrowed values/TZID references expire at release,
destruction or workspace reuse. Numeric POD copies survive independently. Decoder
results do not clear RetainUnreviewed/UnknownParameters or invent omitted values.
Errors expose only categories/value-relative offsets and caller-held property context,
never source payloads. No FiveDaySnapshot transaction is called.

#### Resource and validation evidence

Work is linear in bounded input; writers make validation/measurement then write
passes. Fixed local state and optional caller-owned decoded buffers add no bulk
allocation. A decoded 16 KiB value plus NUL needs 16 KiB + 1 bytes. Reclaim ingestion
slices only after their users finish; no second typed array is reserved. Existing
2 MiB/staging capacities remain provisional, with later recurrence/timezone scratch
and runtime stack/heap/PSRAM headroom unmeasured.

Strict C++11, ASan/UBSan and an additional `-O2` focused run passed 400 result assertions, with zero
audited `new` allocations in decoder/composed calls. Guards cover exact/short buffers,
UTF-8, limits, period context, parameter cardinality, omissions, source immutability,
release/poisoning and arbitrary synthetic HTTP fragmentation/folding. Existing stage
(618 scenarios), structure (565) and ingestion (1,325) regressions pass; the stage
sanitizer also passes. Commands are in AGENTS.md. Initial strict compilation caught
an unused test helper, removed without weakening checks. Final review distinguished
an explicit default VALUE declaration from a forbidden fixed-type override, with
separate tests for default, invalid and unsupported types.
The optimized run initially exposed GCC 14's inlining diagnostic on the test-only
replacement new/delete malloc/free implementation. Keeping those replacement entry
points out of line resolved it without disabling warnings or changing production code.

The isolated fixture composes actual staged decoding inside its existing workspace
scope; it never publishes from the decoder. A clean verbose final-source compile/link
passed on 2026-10-15 with the existing Arduino CLI 1.5.1, Arduino-ESP32 3.3.12,
Xtensa GCC 14.2.0 and XIAO ESP32-S3 Plus/OPI configuration. Explicit library discovery
selected vendored Seeed_GFX2 1.0.0 plus core SPI/Wire, without legacy TFT_eSPI.
Dependency files and ELF symbols establish actual decoder/list inclusion and linkage.
Target assertions verify Date/DateTime/Duration/Period/TemporalValue sizes
**4/8/48/72/80 bytes** and cursor **56 bytes** (host cursor 104); these are layouts,
not measured task-stack peaks. Flash/static globals are **529,663/24,720 bytes**,
against reported limits 3,342,336/327,680. They do not measure runtime PSRAM/headroom.
Core recipes suppress warnings with `-w`; strict host flags do not suppress them.

The invocation uses the existing README tool/config paths, `compile --clean --verbose
--warnings all --jobs 2`, the same XIAO Plus/OPI FQBN and explicit vendored `--library`,
with build output `/tmp/ee02-ics-typed-values-build` and captured log
`/tmp/ee02-ics-typed-values-compile.log`. No dependency installation/upgrade, flashing,
live feed or production build occurred. Host tests and compilation establish no
timezone/recurrence semantics, physical capacity, electrical correctness or complete
event-calendar validity.

### VEVENT-local validation

**Implemented isolated selected checks, not a complete event/calendar validator.**
[application/ics_event_local.h](../application/ics_event_local.h) adds stateless
`IcsEventLocal::validate(stage, calendarIndex, eventIndex, limits)`. It uses accepted
readers/typed decoders without changing ingestion, structure, stage, workspace,
snapshot or production behavior. Report is copied status/deferred bits/property
index/code, with no pointers, content, persistent cache or second event model.

#### Admission, order and bounds

Require successfully readable stage data, the supplied closed Calendar/Event kinds,
their structural ordinal/index association, direct parent and matching calendar
object ordinal. Preserved StagedSuccessfully after release does not admit readers.
No source-wide parent search, UID map or calendar cache is used.

Validate both direct property chains before semantic checks: index/owner bounds,
terminal first/last links and observed count must agree. The accepted append-only
stage's links strictly increase stored indices; rejecting a nonincreasing link also
detects every possible cycle. Children are not part of a parent's direct list.
Caller `maxCalendarProperties`/`maxEventProperties` bound traversal; zero is a bound,
not a product default. Invalid associations and exhausted budgets fail explicitly.

The fixed order is admission/association, traversal/cardinality, required presence,
DTEND/DURATION exclusion, selected typed checks, then relationships. Duplicates
identify the second occurrence in input order. Missing priority is UID, DTSTAMP,
then DTSTART when required; missing indices use IcsSourceStage::None. The second
DTEND/DURATION occurrence identifies a conflict. Typed checks traverse input order.
Failures return no partial-success mask or raw content and cannot be overwritten
by later checks. No source rollback/failure preservation machinery is added.

#### Selected cardinality, METHOD and typed checks

- UID and DTSTAMP are required exactly once. DTSTART is at most once, required
  without an owning-calendar METHOD. METHOD presence, including empty/unsupported
  content, sets MethodSemanticsPending without interpreting any scheduling token.
  Metadata from another VCALENDAR is never substituted.
- CLASS, CREATED, DESCRIPTION, GEO, LAST-MODIFIED, LOCATION, ORGANIZER, PRIORITY,
  SEQUENCE, STATUS, SUMMARY, TRANSP, URL, RECURRENCE-ID, DTEND and DURATION are
  optional singletons; DTEND/DURATION are mutually exclusive. Repeated RFC optional
  fields retain their multiplicity. Repeated RRULE sets MultipleRrulePending and
  RecurrenceValidationPending, not a fabricated last-rule-wins/union result or an
  RFC MUST NOT cardinality error. Extensions get no invented singleton rules.
- UID must be nonempty valid scalar TEXT. SUMMARY/DESCRIPTION/LOCATION use scalar
  TEXT measurement, allowing original empty values where appropriate. No decoded
  text/identity buffer, truncation, case folding or Unicode normalization occurs.
  `maxTextBytes` bounds encoded bytes processed per checked TEXT value; exceeding
  it is ResourceLimit, not truncation. UID equality/uniqueness is never checked.
- DTSTAMP, DTSTART, DTEND, DURATION, RECURRENCE-ID, CREATED and LAST-MODIFIED use
  the actual typed temporal reader and its VALUE/TZID rules. Revision stamps must
  be UTC; second 60 remains pending. SEQUENCE receives only signed RFC INTEGER
  lexical checks, without revision/monotonicity policy.
- Other retained optional fields remain explicitly unvalidated. No URI/address,
  GEO, STATUS enumeration, recurrence list or RECUR validation is introduced by
  this event-local API. It does not invoke the standalone grammar validator below;
  RSCALE/SKIP remains unexamined here and is never reinterpreted as Gregorian recurrence.

#### Local relationships and pending work

DATE endpoints must have matching type and strictly increasing Gregorian fields.
DATE-TIME endpoints must both be UTC or both use local interpretation (RFC 5545
3.8.2.2). Ordinary UTC endpoints are compared as fields and must increase. Local
floating/TZID ordering is deferred, without naive field ordering or same-TZID-string
rules. Leap-second endpoints defer ordering. No epoch or offset conversion occurs.

VEVENT DURATION must be positive/nonzero. A DATE start permits week/pure-day form
only; an explicit time portion is rejected even when its magnitudes are zero, as
in P1DT0H. DATE-TIME durations remain nominal components. Missing METHOD-context
DTSTART causes no fabricated endpoint/default duration; dependent relationships
remain pending. No recurrence master/override comparison or revision selection occurs.

LocalChecksPassed always has SourceValidationRequired and IdentityReconciliationPending.
The remaining distinct bits are MethodSemanticsPending, RecurrenceValidationPending,
MultipleRrulePending, TimezoneResolutionPending, LocalTimeOrderingPending,
LeapSecondPending, ExtensionReviewPending and OptionalPropertyValidationPending.
Recurrence properties remain provisional; relevant unknown values/parameters and
known extensions preserve review obligations. Unchecked retained optional values
remain pending, while approved omitted payloads are never decoded as empty data
or assigned a new extension-review obligation merely for their declared omission.
RFC 9073 child properties and projected opaque components cannot supply event fields.

These bits never establish semantic acceptance. METHOD-specific requirements,
recurrence, timezones, identities/revisions, optional/extension schemas, source
completeness and snapshot transactions remain later work. No new publication model
or FiveDaySnapshot call exists. Source records and previously committed snapshots
are not modified.

#### Lifetime, complexity and evidence

All views/processing stay inside the existing stage `withScratch()` lifetime.
Report has no borrowed references; future validation after release is Unavailable.
There are no allocations, recursion, persistent state or large scratch reservations.
Constant linear passes over the two bounded lists preserve failure precedence;
fixed singleton indices and selected temporary typed operands replace event copies.
Existing workspace capacities remain provisional; stack/heap/PSRAM peaks are unmeasured.

Strict C++11 and ASan/UBSan focused tests passed 150 deterministic scenarios with
zero audited `new` allocations. The actual synthetic pipeline exercises METHOD
scope, all selected singleton rules, missing/error precedence, typed relationships,
deferred obligations, guarded corruption/limits, child isolation, source immutability,
release/poisoning and Content-Length/chunked arbitrary fragmentation. Existing strict
typed (400 assertions), stage (618 scenarios), structure (565) and ingestion (1,325)
regressions pass; the typed sanitizer also passes. Exact commands are in AGENTS.md.
No live feeds, production firmware, recurrence/timezone calculations or physical
EE02 operation were exercised.

An initial clean and final incremental isolated compile/link passed on 2026-10-15
with the existing Arduino CLI 1.5.1, Arduino-ESP32 3.3.12, Xtensa GCC 14.2.0 and
XIAO ESP32-S3 Plus/OPI FQBN from README. The fixture validates one staged pair with
explicit pending bits inside the existing scratch scope, without new publication.
An actual target assertion establishes Report **16/4 sizeof/alignof** (host 24/8).
Dependency output includes this header and ELF symbols include `IcsEventLocal::validate`.
Explicit `--library` selection uses vendored Seeed_GFX2 1.0.0 plus core SPI/Wire;
legacy TFT_eSPI is not selected. Flash/static globals are **532,095/24,720 bytes**,
against reported limits 3,342,336/327,680, not measured runtime memory/headroom.
Core recipes retain their `-w` suppression; strict host checks suppress no warnings.

Both invocations use the existing README tool/config paths and `--verbose --warnings
all --jobs 2`, with build directory `/tmp/ee02-ics-event-local-build`; the first also
uses `--clean`. Logs are `/tmp/ee02-ics-event-local-compile.log` and
`/tmp/ee02-ics-event-local-final-compile.log`. No dependencies were installed/upgraded,
hardware flashed or provider policies selected. These checks establish selected
software behavior and isolated linkage, not complete calendar or hardware correctness.

### VTIMEZONE and observance-local validation

**Implemented isolated local schema checks, not timezone resolution.**
[application/ics_timezone_local.h](../application/ics_timezone_local.h) exposes
stateless `IcsTimezoneLocal::validateTimezone(stage, calendarIndex, timezoneIndex,
limits)` and `validateObservance(stage, timezoneIndex, observanceIndex, limits)`.
Accepted ingestion, structure, stage, typed decoder, event validator, workspace,
snapshot and production implementations are unchanged.

#### Admission, ordering and schema

Require currently readable successfully staged storage, valid supplied indices,
closed Calendar/Timezone or Timezone/Standard-or-Daylight kinds, matching
structural ordinal/index, direct parent and nonzero matching calendar ordinal.
Terminal StagedSuccessfully surviving release cannot admit a reader. No name-based
parent lookup, source-wide scan, child enumeration, TZID index or rule cache exists.
The structural parser already guarantees at least one observance per VTIMEZONE;
a timezone-local pass does not assert that its children passed local validation.

Traverse only the target component's direct append-only property links. Check
owners, stored indices, strictly increasing links (also detecting cycles), terminal
last link and exact observed descriptor count. Explicit maxTimezoneProperties or
maxObservanceProperties bounds every traversal; zero is not unlimited. Parent
metadata is checked without scanning the parent's property list. The source-local
coverage pass below now visits every relevant component separately; complete source
semantics remain later work.

Failure precedence: admission/association, full property-chain integrity/budget,
singleton cardinality, required presence, then selected typed checks in retained
input order. Duplicate reports identify the second singleton occurrence. Failures
have zero deferred bits and bounded property index/code (None for missing fields),
without raw content, staging mutation, durable error state or source rollback.

VTIMEZONE requires one TZID; LAST-MODIFIED and TZURL are optional singletons.
STANDARD/DAYLIGHT each require one DTSTART, TZOFFSETFROM and TZOFFSETTO, with that
missing-field priority. Other fields receive no invented singleton limits. RRULE
SHOULD NOT repeat under RFC 5545 3.6.5; repetition sets pending bits, not a MUST NOT
cardinality error or first/last-rule-wins policy. RDATE may repeat. No extra required
fields, modern regional offset limits or offset-difference requirements are added.

#### Typed checks, omissions and obligations

- TZID uses scalar TEXT measurement, requires nonempty complete retained identity,
  and bounds encoded work by maxTzidTextBytes. There is no truncation, normalization,
  case folding, naming convention, external lookup or cross-definition comparison.
  Parameter semantics remain review obligations.
- LAST-MODIFIED uses the accepted property-local temporal decoder, requiring UTC
  DATE-TIME. It never selects a competing definition. TZURL remains retained and
  optionally unvalidated, with no URI grammar or network retrieval in this unit.
- Observance DTSTART uses the existing typed context: local DATE-TIME without Z or
  TZID. Explicit DATE-TIME VALUE is supported; invalid/unsupported combinations use
  typed error categories. Leap second 60 is preserved as pending confirmation.
- Both offsets use decodeUtcOffset: signed HHMM/HHMMSS, valid individual fields,
  positive zero permitted and negative zero rejected. Identical offsets, unusual
  magnitudes and non-hour changes are not automatically rejected. Offset parameters
  remain available for review; no offset application or instant calculation occurs.
- Each RDATE uses the actual TemporalList with maxRdateMembers/maxRdateBytes.
  These bounds apply independently to each property; bytes include its retained
  value and encoded parameters. Zero budget with a present list is ResourceLimit.
  Complete iteration through End and final Ok/NeedsContext are required. Members
  must be local DATE-TIME; DATE, PERIOD, UTC, TZID, empty/malformed members and an
  invalid suffix fail. No ordering, deduplication, exclusions or expansion occurs.
- RRULE stays unparsed by this timezone-local API, including RSCALE/SKIP; the separate
  grammar API below is not invoked here. EXDATE in observances is not a core
  tzprop under RFC 5545: preserve it with ObservanceExdateReviewPending and
  ExtensionReviewPending, without applying, dropping or inventing extension support.
- Declared TZNAME/COMMENT payload omissions remain distinct from original empty
  values. Do not decode them or fabricate missing required timezone information.
  Retained review flags survive. Unknown relevant properties/parameters and known
  uninterpreted extensions retain explicit review obligations; recognized retained
  optional fields without selected typed checks remain optionally unvalidated.

Every successful result has SourceValidationRequired and TransitionResolutionPending.
Timezone success additionally has TimezoneIdentityReconciliationPending. Applicable
bits are RecurrenceValidationPending, MultipleRrulePending, ExtensionReviewPending,
OptionalPropertyValidationPending, LeapSecondPending and ObservanceExdateReviewPending.
These are obligations, never semantic acceptance. No transition completeness,
resolved TZID, UTC event timestamp, complete source or snapshot outcome is returned.

#### Lifetime and evidence boundaries

All processing completes inside the stage's existing withScratch lifetime. Report
is a small pointer-free POD; there is no decoded timezone table, second property
pool, allocation, recursion or persistent state. Constant bounded direct-list
passes plus linear bounded TEXT/RDATE work use fixed local counters and temporary
typed PODs. Candidate workspace capacities are unchanged; runtime stack, internal
heap, PSRAM headroom and energy remain unmeasured. Physical EE02, live providers,
complete firmware, transition/recurrence rules and timezone conversion are not
validated by this isolated unit.

Strict C++11 and ASan/UBSan focused runs passed **202 deterministic scenarios**
with zero audited validator `new` allocations and guarded byte-for-byte source
immutability. Cases use actual complete synthetic framing/structure/staging,
Content-Length/chunked fragmentation/folds, invalid associations/links, exact and
over-budget work, deferred extensions/EXDATE, failed upstream completion and
release/poisoning. Existing strict typed/stage/structure/ingestion and VEVENT-local
regressions passed 400 assertions / 618 / 565 / 1,325 / 150 scenarios respectively.
Commands are in AGENTS.md; no unrelated snapshot/framebuffer/acquisition regression
was needed because their implementations and contracts are unchanged.

An initial clean and final incremental isolated compile/link passed on 2026-10-15
using the existing README Arduino CLI 1.5.1 / ESP32 core 3.3.12 / GCC 14.2.0 tools,
XIAO ESP32-S3 Plus OPI FQBN and explicit vendored Seeed_GFX2 1.0.0 (plus core
SPI/Wire). The extended fixture invokes both operations inside existing scratch;
new dependency records and the linked common `IcsTimezoneLocal::detail::validate`
implementation establish inclusion/linkage, with primary inline wrappers inlined.
Report sizeof/alignof is **24/8 host, 16/4 target** (target static assertion).
Final flash/static globals are **533,967/24,720 bytes**, reported maxima
3,342,336/327,680, not runtime memory measurements. The clean build emitted the
three previously documented vendored Seeed_GFX.cpp warnings; no unrelated fixes
or warning-free target claim is made. Core recipe `-w` suppression remains.

Both commands use the existing README tool/config paths, `--verbose --warnings all
--jobs 2`, explicit `--library`, and `/tmp/ee02-ics-timezone-local-build`; the initial
also uses `--clean`. Logs: `/tmp/ee02-ics-timezone-local-compile.log` and
`/tmp/ee02-ics-timezone-local-final-compile.log`. Official RFC 5545 3.6.5/3.8.3
provided schema, local-onset and identity evidence. No dependencies were installed
or upgraded, live calendars accessed, hardware flashed or product policy selected.

### Source-local validation coverage

**Implemented local-check coverage, not complete calendar/source semantics.**
[application/ics_source_local.h](../application/ics_source_local.h) exposes stateless
`IcsSourceLocal::inspect(stage, limits)`. It composes the accepted local validators
without modifying parser, stage, typed values, event/timezone local checks,
workspace, independent snapshot or production behavior. No source-completion state
machine, persistent object, second event/component store, identity map or new
allocation owner exists.

#### Source-order enumeration and dispatch

Require StagedSuccessfully plus actual readable component storage; terminal status
surviving release is insufficient. Require a nonempty, size_t-representable component
count within maxComponents. Walk every staged component index in order, checking
ordinal == index + 1, closure, valid earlier parent, matching calendar-object
ordinal and accepted parent kind. Track the active Calendar index explicitly:
calendar ordinal is the successive root identity, not its structural ordinal or
stored index. Each Calendar has no parent, advances that identity consistently,
and the final inspected root count must match stage counts().calendars.

Invoke IcsEventLocal::validate for each direct Calendar/Event pair,
validateTimezone for each direct Calendar/Timezone pair, and validateObservance for
each direct Timezone/Standard-or-Daylight pair. Pass supplied local limits unchanged.
No source-wide parent search, UID/TZID lookup, component sorting or five-day filtering
selects records. Caller budgets independently bound roots, events, timezones and
observances; all descriptors, including nested/projected ones, count toward the
source component budget. Checked count increments admit exact limits and reject
exhaustion without wraparound; zero is never an unlimited budget.

For each root, validate its entire direct stored-property chain within
maxCalendarProperties before recording pending metadata. Check reader/index/owner,
strictly increasing append-only links (including cycle protection), terminal
first/last links and exact observed count. Observe METHOD, CALSCALE, retained
relevant unknown/extension metadata, unreviewed/unknown-parameter flags and retained
additional metadata lacking selected value semantics as CalendarMetadataPending.
VERSION/PRODID requirements and root closure/component presence remain owned by
IcsStructure; this pass adds no competing root schema, METHOD token, calendar-system,
URI, source-fetch or scheduling interpretation. Omitted values remain unavailable,
never fabricated empty originals. Structural requirements alone do not establish
complete root semantics.

Retained event-associated PARTICIPANT/VLOCATION/VRESOURCE components set
NestedComponentReviewPending without validating or merging their UID/content into
VEVENT. VTODO/VJOURNAL/VFREEBUSY, VALARM and their projected descendants respect the
stage projection. Each ProjectedOut descriptor outside the calendar/event/timezone/
observance groups counts once in projectedNonEventComponents and sets
NonEventProjectionObserved. This includes accepted opaque and nested opaque
components. Opaque observed properties have no stored property descriptors; their
links are not traversed to enumerate omitted payload. No opaque UID/DTSTART/SUMMARY/
RRULE is treated as event data. Inconsistent parent kinds/projection flags and
unexpected kinds fail association checks; no new nesting relationships, structural
ordering grammar or extension registry are introduced.

#### Results, obligations and first failure

Report contains only copied status, FailureKind, component/property indices/code,
separate original event/timezone status types, three uint32_t masks and counts.
Missing diagnostic indices use IcsSourceStage::None. Local-status slots are failure
diagnostics only for the corresponding FailureKind; inactive LocalChecksPassed
placeholders do not prove that a call occurred. No enum-domain casts or borrowed
views escape in Report.

Order is availability, source component capacity, then for each component:
descriptor/association, relevant count budget, local validator or root property
chain, then accumulation. The first failure wins. Child failures use
EventLocalFailure, TimezoneLocalFailure or ObservanceLocalFailure with the original
typed local status/property diagnostic. Local ResourceLimit remains a child failure,
while source-budget/root-chain failures use source statuses. Failed reports zero
all masks and successful-coverage counts; earlier successes never become partial
published coverage. No staging mutation, durable error, rollback or snapshot call
occurs.

Success always sets SourceSemanticsPending; OR event-local masks into eventDeferred
and timezone/observance masks into timezoneDeferred, preserving their distinct enum
domains and every bit. Additional source observations are CalendarMetadataPending,
NestedComponentReviewPending and NonEventProjectionObserved. Successful counts prove
full enumeration/applicable local success only. Zero-event, projected-only and
opaque-only sources may pass coverage while supported source policy and valid
empty-schedule semantics remain unresolved. METHOD, recurrence, identities/revisions,
TZID matching, transitions, nested/extension/detail semantics, normalization and
source publication remain the later semantic producer's responsibility.

#### Lifetime, resources and evidence limits

All reads/calls finish inside the stage's existing withScratch source lifetime.
Reports survive release without aliases; new inspection after release is Unavailable.
The pass is C++11, read-only, nonrecursive and allocation-free, using fixed local
counters, indices and small reports. No dedicated scratch slice or final operating
capacity is selected. Work is one bounded component walk plus bounded root metadata
and existing local-validator work. The accepted event validator also checks its
owning root properties per event; no source-wide rescan or cache replaces that
existing guarantee. Physical stack/heap/PSRAM/CPU headroom remains unmeasured.

Fresh strict C++11 and ASan/UBSan coverage runs passed **106 deterministic
scenarios** with zero audited `new` allocations, guarded byte-for-byte storage
immutability and unchanged stage counters/status. Synthetic Content-Length/chunked
responses exercise folds/fragmentation, distinct multi-root identities, complete
component dispatch, later failures, full separate masks, metadata/projection rules,
exact/over budgets, deliberate association/link corruption and failed/released
storage. The copied Report remains readable after stage release/poisoning; host
sizeof/alignof is **88/8**, not an established target layout or memory headroom.
Fresh accepted strict VEVENT-local and timezone-local regressions passed **150/202
scenarios**. Actual commands are in AGENTS.md.

These results belong to this new implementation, not to the unavailable unpublished
commit. Earlier compiler/target/hardware evidence remains scoped to its original
units. No accepted staging/structural implementation changed or shared defect was
exposed, so additional shared regressions were unnecessary. No unrelated snapshot,
graphics/acquisition tests, new target/production build, live feed or physical
hardware check ran. The GFX2 fixture was not extended without a material target
concern. Recurrence/timezone correctness, complete source semantics, production
integration and runtime memory/CPU fit remain unverified.

### Bounded RECUR core grammar validation

**Implemented standalone representation checks, not recurrence application or
calendar completeness.** [IcsRecurValue](../application/ics_recur_value.h) exposes
`validate(IcsStructure::View, Limits)`. The input is one complete, readable,
length-delimited retained RRULE value; no NUL terminator or trimming is assumed.
No decoder, local-validator, stage, workspace, snapshot or production interface
changed. IcsEventLocal, IcsTimezoneLocal and IcsSourceLocal retain their recurrence
obligations and do not call this API; the separate wrappers below compose it.

#### Grammar, bounds and deterministic passes

RFC 5545 [section 3.3.10](https://www.rfc-editor.org/rfc/rfc5545#section-3.3.10)
owns the core grammar and local constraints. Rule names, frequency and weekday
tokens are case-insensitive. Receivers accept parts in any order; the generating
interoperability requirement to put FREQ first is not imposed on input. FREQ must
appear exactly once; other recognized core names cannot repeat. UNTIL/COUNT are
exclusive. BYMONTHDAY/WEEKLY, BYYEARDAY/DAILY-WEEKLY-MONTHLY, BYWEEKNO/non-YEARLY,
numeric BYDAY/non-MONTHLY-YEARLY or YEARLY-with-BYWEEKNO, and BYSETPOS without
another BY part fail locally. Repeated members within one list remain unchanged.

Three bounded passes establish deterministic failure precedence:

1. Check argument spans and the total byte budget. Scan complete NAME=VALUE
   framing, token names, nonempty parts/values, duplicate core names, required
   FREQ and structural part/aggregate BY-list-member budgets. Values remain opaque
   at this stage apart from extra equals signs and empty BY-list members. Missing
   FREQ is diagnosed at input end. Only after the complete framing scan succeeds,
   return UnsupportedFeature at the first unknown/extension name, if any.
2. For extension-free input, validate core values in input order. Numeric members
   obey their exact RFC digit widths, sign permissions and ranges. COUNT/INTERVAL
   require positive unsigned decimal uint64 magnitudes; overflow is ResourceLimit
   at the overflowing digit, not malformed syntax or a permanent producer limit.
   UNTIL uses accepted DATE/DATE-TIME primitives; DATE, floating and UTC forms are
   permitted here. [Verified erratum 4414](https://www.rfc-editor.org/errata/eid4414)
   removes the contradictory universal UTC sentence. Actual DTSTART/observance
   compatibility is checked by the separate contextual wrapper below. UNTIL second
   60 or BYSECOND=60 records pending
   leap-second confirmation, never instant validity.
3. After core representations succeed and FREQ is known, examine cross-part
   constraints in input order. UNTIL/COUNT identifies the later conflicting name;
   other relationships identify the offending BY name, or first numeric BYDAY
   member. No date/candidate generation, DTSTART inference or epoch conversion occurs.

Framing errors use the offending byte, an expected missing byte at input end, or
the second duplicate name. Numeric syntax/width errors use the offending byte;
range/zero failures use the member start. Unsupported features use their first
name offset. These are zero-based value-relative offsets without source text.
maxRuleBytes diagnoses its first excess byte; part/member exhaustion diagnoses the
next part/member start. Exact budgets pass; zero is not unlimited. Span arithmetic
rejects null nonempty input, PTRDIFF_MAX excess and address wrap before reading;
actual readable ownership remains the caller's responsibility.

[RFC 7529 section 4](https://www.rfc-editor.org/rfc/rfc7529#section-4) changes
numeric ranges and permits leap-month suffixes. RSCALE, including GREGORIAN, SKIP
with or without RSCALE, and other well-framed unrecognized names therefore return
UnsupportedFeature before Gregorian-specific value/range/relationship checks.
Their grammar and semantics are not validated or ignored; malformed generic framing,
missing FREQ, repeated core names and resource limits still fail explicitly. No
non-Gregorian subset or general extension registry is selected.

#### Results, lifetime and validation limits

Report is pointer-free status/offset, Frequency, core presence mask and a boolean
leapSecondPending. Non-success clears frequency to Unknown, mask to zero and pending
leap evidence to false. CoreGrammarValid establishes only these representation and
local relationship checks. The separate contextual API below now checks DTSTART/
UNTIL representations and reports DATE time-part ignores; the core grammar API
remains context-free. Multiple-RRULE reconciliation, RDATE/EXDATE application,
revision/override reconciliation, timezone resolution, expansion profile and source
completeness remain future work. No component/source status is modified or snapshot
call made.

The implementation is allocation-free C++11, nonrecursive, linear in admitted
bytes with a fixed number of passes and fixed-size local state. It retains no
numeric/list database, separate byte pool, PSRAM slice or event record. Borrowed
stage values must remain readable for the call and expire at release/reuse;
Reports survive without them. maxRuleBytes/maxParts/maxListItems are caller-supplied
evaluation controls, not final operating capacities. Physical stack/CPU/heap/PSRAM
headroom and target/production compatibility remain unmeasured.

Fresh strict C++11 and ASan/UBSan runs of tests/ics_recur_value_test.cpp passed
**369 deterministic cases** with zero audited `new` allocations, unchanged guarded
source bytes and pointer-free reports surviving input/stage poisoning. Coverage
includes core values/relationships, rotated part order, pass-ordered offsets,
extension-before/after behavior, exact/excess budgets and synthetic Content-Length/
chunked one-byte ingestion through actual stage readers. Original local reports,
stage bytes/counts and recurrence obligations remain unchanged. Host Report
sizeof/alignof is **32/8**; this is not a target layout or operating-headroom claim.
The accepted strict typed-value regression passed **400 result assertions**, covering
the primitive decoders directly composed for UNTIL. Commands are in AGENTS.md.

No protected implementation changed or shared boundary defect emerged, so additional
local-validator/staging/structure regressions were not needed. No unrelated snapshot,
graphics/acquisition tests, isolated target build, production build, live-provider
call or hardware check ran; no graphics fixture or dependency changed. Earlier
target/hardware evidence remains scoped to its original units. These fresh results
do not establish an expansion profile, recurrence/timezone correctness or complete
source semantics.

### Contextual RRULE validation

**Implemented locally determinable representation compatibility, not executable
recurrence or source completeness.** [IcsRecurContext](../application/ics_recur_context.h)
exposes stateless `validate(stage, parentIndex, componentIndex, rrulePropertyIndex,
limits)`. Callers select one actual retained rule; there is no automatic rule choice,
combination, source enumeration, UID/TZID index, cache or second recurrence/event
representation. Existing grammar, typed, local/coverage, stage and snapshot
implementations/interfaces remain unchanged. Source-local coverage does not call
this operation. The separate VEVENT recurrence-property pass below now enumerates
event rules; source-wide recurrence coverage, including observances, remains later work.

#### Admission, order and diagnostics

Require StagedSuccessfully plus currently readable storage. Both supplied components
must exist, be closed, match ordinal/index and calendar-object identities, have the
correct direct parent association and remain outside projected/opaque contexts.
Accept only Calendar/Event or Timezone/Standard-or-Daylight pairs; parents precede
children. The selected property must exist, belong to the component, have Rrule code
and an actually retained readable value. Root, task/journal/freebusy, alarm, opaque
and RFC 9073 child content cannot supply recurrence context.

Order is admission, applicable local validation, complete bounded direct-chain scan,
core grammar, then selected context checks. Invoke IcsEventLocal::validate or
IcsTimezoneLocal::validateObservance with exact supplied indices and unchanged local
limits. A local failure returns LocalValidationFailed in EventLocal/ObservanceLocal
domain, preserving original typed status and property diagnostics, without grammar
interpretation. No source-local coverage call substitutes for this precondition.

After local success, independently traverse the entire direct property chain within
maxComponentProperties: owner/index bounds, increasing links/cycle protection,
first/last terminal consistency and exact observed property count must agree. Record
only DTSTART index, RRULE count and requested-rule membership; an existing property
claiming ownership but absent from the chain fails association. The budget is exact,
zero is a bound and counters are checked; limits select no permanent operating quota.

Call the public core RECUR validator with unchanged rule limits. Preserve its typed
diagnostic and rule-relative offset in RuleGrammar domain while mapping malformed,
unsupported, resource and argument failures to corresponding contextual statuses.
Grammar failures cannot become pending successes, including with missing DTSTART.
Admission/chain/relationship errors use Context domain; success and missing-start
outcomes use None. Invalid indices/non-RRULE arguments are InvalidArgument; wrong
ownership/kinds/hierarchy are InvalidAssociation; unavailable/omitted values are
Unavailable. A context scan budget is ResourceLimit; a local validator's resource
failure remains LocalValidationFailed with its original local status.

Report contains only status/domain, property index/code, rule-relative offset,
separate original grammar/event/observance status types and two uint32_t masks.
Inactive typed slots are placeholders and provide no validation evidence. Success/
pending outcomes have no failing-property index/code. Component-only diagnostics use
IcsSourceStage::None; an explicit invalid property index is reported as supplied.
UNTIL relationship failures identify the selected RRULE and
its UNTIL value's zero-based offset. Error reports zero both masks. No source text,
UID/TZID, URL, borrowed view, durable failure, rollback or snapshot operation occurs.

#### DTSTART, UNTIL and ignored evidence

Use the accepted temporal decoder for direct DTSTART and retain DATE/floating/UTC/
TZID distinctions without epoch conversion. A TZID-associated start sets
TimezoneResolutionPending; its definition need not already exist or match an IANA
name. Parsed DTSTART or rule leap-second evidence sets LeapSecondPending without
confirmation or rewriting. These are representation observations, even if a valid
time part is later reported ignored; no occurrence receives an applied leap second.

After core grammar success, a bounded scan of validated part boundaries locates
the complete UNTIL value and uses existing DATE/DATE-TIME primitives. This is not a
second RECUR grammar parser. RFC 5545 3.3.10 with verified erratum 4414 requires:

| Owning component / DTSTART | Required UNTIL when present |
| --- | --- |
| VEVENT DATE | DATE |
| VEVENT floating DATE-TIME | Floating DATE-TIME, no Z |
| VEVENT UTC DATE-TIME | UTC DATE-TIME with Z |
| VEVENT TZID-associated DATE-TIME | UTC DATE-TIME with Z |
| STANDARD/DAYLIGHT local DTSTART | UTC DATE-TIME with Z |

Mismatch is InvalidRelationship. No universal UTC rule is imposed on events. UNTIL
absence invents nothing, and chronological comparison with DTSTART is not performed.
No pattern-membership test, gap/fold handling, offset application, inclusive filtering,
five-day selection, candidate generation or expansion subset is chosen.

The same RFC requires receivers to ignore BYSECOND/BYMINUTE/BYHOUR for DATE DTSTART.
After full core grammar succeeds, copy exactly those presence bits into
ignoredCoreParts and set IgnoredTimePartsPresent. Invalid core parts still fail;
DATE-TIME starts ignore none under this rule. Original properties stay unchanged;
there is no rewritten rule or expanded result.

A METHOD-bearing VEVENT may locally pass without DTSTART. Validate grammar first,
then return MissingStartContext with MissingDtstartPending and other already known
obligations, without imposing DTSTART-dependent UNTIL or ignore rules. Ordinary
missing starts and missing observance starts fail their existing local validators.
METHOD tokens, scheduling support and source completeness are not adjudicated here.

#### Pending obligations and lifetime

ContextChecksPassed and MissingStartContext always retain RecurrenceExpansionPending
and RecurrenceSetReconciliationPending. More than one direct RRULE adds
MultipleRrulePending; only the requested property is validated, without union,
precedence or validating siblings as a side effect. Any encoded RRULE parameters
add ParameterReviewPending; VALUE/TZID/extension parameters do not change RECUR
interpretation or UNTIL matching. Original parameters/review flags remain unchanged.
Other selected bits are MissingDtstartPending, TimezoneResolutionPending,
LeapSecondPending and IgnoredTimePartsPresent. No mask establishes executable
recurrence, parameter support, resolved timezones or a complete calendar.

All borrowed processing finishes inside the existing stage withScratch lifetime.
Reports survive release without aliases; later validation is Unavailable even when
the stage's success status survives release. Fixed local indices/counters and typed
POD fields need no allocation, recursion, extra PSRAM slice, persistent storage or
decoded recurrence array. Work is bounded by existing local validation, one direct
component scan, core grammar work and at most one validated-rule UNTIL scan. Physical
stack/CPU/heap/PSRAM headroom and production/target/hardware behavior remain unmeasured.

Fresh strict C++11 and ASan/UBSan runs of tests/ics_recur_context_test.cpp passed
**173 deterministic cases** with zero audited `new` allocations, guarded storage,
byte-for-byte source immutability and unchanged stage status/counts. Synthetic
Content-Length/chunked feeds exercise one-byte and other splits through actual
ingestion, structure and staging. Cases cover both event/observance paths, the
UNTIL matrix, independent repeated rules, parameters, missing starts, original
failure diagnostics, exact/excess limits, corrupt associations/links, release and
report survival after poisoning. Context checks leave existing source-local reports
and pending obligations unchanged. Host Report sizeof/alignof is **56/8**, not a
verified target layout or operating capacity.

Fresh strict regressions also passed: **369** core RECUR cases, **150** VEVENT-local
scenarios and **202** timezone/observance-local scenarios. These directly composed
preconditions/results were checked without changing their implementations. Commands
are in AGENTS.md. No shared defect or protected-interface change justified additional
staging/structure suites. No unrelated snapshot/graphics/acquisition tests, target or
production build, provider operation or physical hardware check ran. Previous target
evidence remains scoped to its original units; these results establish no recurrence
expansion, timezone resolution, source completeness or production compatibility.

### VEVENT recurrence-property local coverage

**Implemented complete local enumeration within one VEVENT, not a defined recurrence
set or complete calendar.** [IcsEventRecurrenceLocal](../application/ics_event_recurrence_local.h)
exposes stateless `inspect(stage, calendarIndex, eventIndex, limits)`. It composes
the accepted event-local precondition, every contextual RRULE check and complete
typed RDATE/EXDATE iteration. It adds no new parser, recurrence database, UID/TZID
index, persistent object, PSRAM allocation, source acceptance or snapshot transaction.
Source-local coverage is unchanged and does not call this pass automatically.

#### Admission, traversal and diagnostics

Require StagedSuccessfully and readable storage; status surviving release is not
sufficient. Validate supplied Calendar/Event kinds, root/direct-parent association,
closed components, ordinal/index consistency, matching nonzero calendar ordinals,
parent-before-child order and absence of projected/opaque flags. Invalid indices
are InvalidArgument, unavailable stages are Unavailable, and association errors are
InvalidAssociation, in Association domain. No source-wide component search occurs.

Failure precedence is admission, IcsEventLocal::validate with limits.context.event
unchanged, full direct-chain integrity/budget, then recurrence processing in stored
property order. Event-local errors return EventLocalFailure with their original
typed status/index/code; this includes corruption already detected by that required
precondition. The independent chain preflight checks every owner/index, strictly
increasing link, terminal first/last pair and exact observed descriptor count within
maxComponentProperties. It completes before any recurrence-property interpretation.
Children cannot supply their parent's recurrence fields.

The second direct-chain pass handles every recurrence property. maxRruleProperties,
maxRdateProperties and maxExdateProperties independently admit exact limits and
reject excess. Zero is a real bound; no permanent capacities are selected.
Checked counters and subtraction-based byte sums reject exhaustion without wrapping.
No malformed later property can be hidden by earlier successful properties.

Report contains copied status/domain, property index/code, value-relative offset,
separate original event/context/typed statuses, original context failure domain,
two uint32_t masks, property/member counts and recurrenceIdPresent. Diagnostic
slots are meaningful only for their active domain; inactive slots are placeholders.
Unavailable property indices use IcsSourceStage::None. Error reports zero both masks
and all successful counts; no provisional member or earlier component result is
published. No source text, title, UID, TZID or URL appears in diagnostics.

#### Rules and aggregate date-list budgets

Every RRULE invokes IcsRecurContext::validate with exact indices and unchanged
context limits. ContextChecksPassed and MissingStartContext are pending outcomes;
other results return RruleContextFailure with original context status/domain,
property and offset. Multiple rules remain independent and unresolved, including
when a later rule is malformed/unsupported. No first/last selection, union or
Gregorian fallback for RSCALE/SKIP is introduced. Missing start context and original
DATE time-part-ignore evidence survive in contextualRuleDeferred, which ORs all
original contextual bits without conflating enum domains.

For each direct RDATE/EXDATE, require a retained readable value and account for its
complete value plus encoded parameters. One maxTotalListBytes and one
maxTotalListMembers apply across BOTH property classes. Before each iterator,
require positive remaining allowances and check the byte sum by subtraction; pass
the remaining member/byte allowances unchanged to TemporalList. After every parsed
member, decrement the shared remaining-member allowance and increment the owning
class's member count. Complete through End and require complete() plus final
Ok/NeedsContext before admitting that list. Exact last-member exhaustion succeeds;
a further list/member fails explicitly. Only completed lists consume aggregate
bytes; all intermediate accounting remains provisional until the whole pass succeeds.

Use the accepted selector/iterator for RDATE DATE-TIME/DATE/PERIOD and EXDATE
DATE-TIME/DATE. Preserve its parameter/cardinality, type, empty-member, malformed
temporal and PERIOD behavior; EXDATE PERIOD remains invalid. No parallel splitter
or decoded member array exists. Invalid/unavailable list values return
TemporalListFailure in TemporalList domain, preserving the original typed status
and offset. Iterator ResourceLimit uses outer ResourceLimit with TemporalList domain
so its original diagnostic remains meaningful. Explicit wrapper budgets use
ResourceLimit/Budget instead. A contextual validator's resource failure remains
RruleContextFailure with its original contextual diagnostic.

Every list sets DateListContextPending and recurrence-set reconciliation pending,
even when its primitive values need no additional context. Valid mixed list types,
floating/UTC representations or different TZIDs are not compared to DTSTART or one
another. TZID members add TimezoneResolutionPending; parsed leap seconds add
LeapSecondPending. PERIOD adds PeriodSemanticsPending; local PERIOD ordering and
other NeedsContext conditions remain pending. Parameters requiring review retain
ParameterReviewPending. No duration/end policy, instance generation, exclusion
application, deduplication or recurrence-set compatibility is selected.

#### Override evidence, obligations and lifetime

RECURRENCE-ID cardinality/type/temporal checks remain exclusively in the accepted
event-local validator. This pass detects presence and inspects existing parameter
metadata only: set recurrenceIdPresent, OverrideIdentityPending and recurrence-set
reconciliation; RANGE adds RangeReviewPending, and other unvalidated parameters
retain ParameterReviewPending. No second temporal decoder, master lookup, revision
selection, cancellation or moved-instance behavior is introduced. An override-shaped
event without RRULE is not promoted into a recurrence master.

Success always has SourceSemanticValidationPending. Any direct recurrence property
adds RecurrenceSetReconciliationPending. Rules add RecurrenceExpansionPending and
explicit mappings of their missing-start/multiple-rule/parameter/timezone/leap
obligations, while their original mask stays separate. Lists and overrides retain
their respective context/identity obligations without inventing a recurrence rule.
A no-recurrence VEVENT may succeed with zero counts and only source validation
pending. None of these outcomes establishes a valid empty source, defined executable
set, resolved identity/timezone, expansion profile or snapshot eligibility.

All borrowed processing ends inside the existing withScratch lifetime. Fixed-size
indices/counters, reports and one temporary iterator/member require no allocation,
recursion, bulk representation or new memory owner. Existing validators may repeat
their own bounded checks; no decoder is duplicated to avoid those checks. Reports
survive release/poisoning; subsequent reads are Unavailable. Final capacities and
physical stack/heap/PSRAM/CPU headroom remain unmeasured. This unit is VEVENT-only;
the observance EXDATE contract and other component/source policies are unchanged.

Fresh strict C++11 and ASan/UBSan runs of tests/ics_event_recurrence_local_test.cpp
passed **160 deterministic cases**, with zero audited `new` allocations, guarded
byte-for-byte source immutability and unchanged stage status/counts. Real synthetic
Content-Length/chunked responses use one-byte and fragmented delivery through
ingestion, structure and staging; transient parser buffers are poisoned before
coverage. Tests exercise complete rule/list enumeration, later failures clearing
all provisional masks/counts, exact/excess combined budgets, checked byte arithmetic,
unchanged mixed representations, RANGE/override evidence, association corruption,
repeatability and copied reports surviving release/poisoning. Host Report
sizeof/alignof is **104/8**, not a target-layout or measured headroom claim.

Fresh accepted strict regressions passed: **173** contextual RRULE cases, **369** core
RECUR cases, **150** VEVENT-local scenarios and **400** typed/list result assertions.
Actual commands are in AGENTS.md. No accepted implementation/interface changed or
shared defect emerged to justify additional staging/structure suites. No unrelated
snapshot/graphics/weather/acquisition tests, target/production build, live-provider
operation or hardware check ran; no target compatibility concern was exposed.
Historical evidence remains scoped to its original units. These results do not
establish recurrence-set semantics, expansion, resolved timezones, source completeness
or production/hardware compatibility.

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
weather live in ordinary static RAM; production uses no normalized snapshot.

No OpenAI requests, artwork processing/rendering, application filesystem operations
or persistent data cache remain. The removed artwork path owned the overlapping
4MiB+1 response and variable-size decoded PNG PSRAM allocations, prompt strings,
decoder/file state, static dither/error storage and row buffers. This establishes
removed allocation sites, not measured memory savings or verified headroom. No new
workspace, persistent state or framebuffer ownership mechanism is introduced into
the production application; isolated workspace/borrowing/snapshot/ingestion code is described above.
Display-driver/core allocations and actual stack/heap/PSRAM peaks remain unverified.
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

The isolated source stage holds calendar content only in volatile borrowed PSRAM;
it adds no NVS/flash writes. Its diagnostics contain codes and ordinals rather than
content. Release invalidates views but is not secure erasure; physical-access
protection and runtime content handling still require the existing security review.

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
No general runner, CI, reproducible production firmware compile command or documentation
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
- **Selected GFX2 production integration/hardware unverified:** the pinned import,
  local panel-borrowing extension, scoped application workspace, normalized snapshot, focused host ownership
  tests and isolated exact-target compile/link evidence are present. Production migration, runtime PSRAM/
  heap/stack measurements, orientation and physical panel behavior remain pending.
  Isolated compilation establishes neither a complete production build nor memory
  reliability on EE02. Graphics-foundation selection is no longer Open.
- **Alpha core reproducibility unverified:** cached package metadata includes
  4.0.0-alpha1, but availability, complete installation, exact board recipes and
  a successful production build are unverified. Separately, the isolated GFX2
  fixture uses the available stable 3.3.12 toolchain; this changes no production settings.
- **No complete build/regression baseline:** documented library versions are not
  compatible revision proof; credentials/configuration, memory and partition fit
  remain prerequisites for a verified complete build. Host coverage is limited
  to acquisition orchestration and isolated panel/workspace/snapshot/ICS contracts,
  including staged values, local coverage, core RECUR grammar, contextual RRULE
  checks and VEVENT recurrence-property coverage; no CI is committed.
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
legacy month grid, waking-hour defaults, recurrence/producer limits and final snapshot capacities, ICS-only
transport commitment versus private CalDAV/Google authenticated access, weather
freshness and retained-buffer fit, failure state transitions, actual button reset
mapping, compatible GFX2/core/board integration and verified PSRAM/transfer
behavior (direct DMA remains unverified, not required), refresh default/battery-life targets,
and hardware pilot acceptance. Current constants do not decide these.

Seeed_GFX2, application-owned phase reuse, the independent PSRAM snapshot,
sequential streaming and per-calendar transactional publication are **Selected**,
not Open alternatives. Numeric evaluation budgets are provisional, not validated
limits. Contrary engineering evidence requires explicit review and adjudication.

**Recommendations not yet selected:** Arduino CLI as the reproducible production build route,
immutable compatible pins for other dependencies, broader focused C++ host coverage, explicit
production source-results/time normalization, durable configuration-save mechanisms and
authenticated TLS mechanisms. The initial GFX2 source pin is approved and imported;
its local borrowing, scoped workspace and synthetic snapshot are implemented with isolated compile/link
evidence. Production integration and physical memory/display behavior remain
unverified, not grounds for a silent source upgrade.
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

On 2026-10-17, the owner made verified safe feature-branch publication mandatory
for every completed authorized tracked Codex execution unit in the same run,
after applicable local validation and commit. This supersedes the discretionary
nature of the earlier publication permission, preserving repository/destination
and ancestry verification, non-force publication and independent-review safeguards.
Blocked publication still requires local preservation and an explicit incomplete
publication report. [AGENTS.md](../AGENTS.md#authorized-feature-branch-publication)
remains the current workflow and publication-procedure authority.

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

On 2026-10-11, the owner approved the previously researched GFX2 revision
`8f74b7aa91c4fcbc5ba2467bab4de4e7ce0eeeeb` for unmodified source integration and
validation. This supersedes its unapproved-pin status, not earlier evidence limits:
toolchain and physical compatibility remain unverified. The authorized outcome is
one locally maintained vendor baseline and isolated compile/link fixture, preserving
the active renderer; framebuffer borrowing and production migration remain separate.

On 2026-10-12, the owner authorized the narrow local `Panel_EPaper` borrowing
extension. Only those two vendor source files diverge from the immutable import.
Successful panel end forgets the borrow; destruction forgets pointers without
proving quiescence. No separate release API or general allocation/recovery framework
was needed for the selected wake sequence.
The implementation rejects borrowed mirroring, preserving ordinary owned behavior;
physical orientation, electrical shutdown and runtime memory remain unverified.
This records library implementation scope, not production migration or hardware approval.

On 2026-10-12, the owner authorized the scoped application PSRAM workspace outcome
and adjudicated direct scratch access (no permanent copy-only parser restriction)
and uncertain shutdown as a restriction on memory reuse, not software recovery.
The isolated workspace and tests implement that contract without production
migration, a snapshot schema/allocation, calendar processing or a fixed 2 MiB limit.
Historical borrowing/source-import evidence remains distinct from this unit's
host/compile results and from still-unverified physical/runtime behavior.

On 2026-10-12, the owner adjudicated the normalized snapshot model: original UTC
timed intervals and points, civil-day all-day intervals, six supplied UTC boundaries
for five consecutive local dates, complete owned UTF-8 text and a preinitialized source catalogue.
The independent snapshot now implements one append-checkpoint transaction at a
time with Complete/Incomplete publication and failure rollback. This supersedes
the earlier unimplemented/Open snapshot-schema and overflow-behavior descriptions;
final capacities and provider/timezone/recurrence normalization remain Open.
Synthetic host/build evidence changes no production acquisition or hardware claims.

On 2026-10-12, independent plan review approved bounded raw HTTP framing and
strict ICS line unfolding, with a mandatory correction limiting redirects to
header validation and an untrusted handoff. The implementation follows that
boundary. Earlier same-origin/redirect-chain planning recommendations were not
selected policy; URL resolution, following, credentials and certificates remain
separate future adjudication. No production migration or semantic parser was authorized.

On 2026-10-13, independent review approved the bounded structural parser unit
with explicit RFC 9073 relationships. This supersedes unimplemented property/
component parsing descriptions in isolated current-state authority, while retaining
transient ownership, separate staging and deferred semantic normalization. It does
not select provider access, semantic extension coverage or final operating limits.

On 2026-10-15 the independently approved selective source-local retention outcome
added descriptor/pool staging and explicit non-event/value projections. Its opaque
component handling follows RFC 5545 section 3.6 while retaining relevant unknown
properties separately. This supersedes isolated unimplemented-staging descriptions;
semantic processing, production integration and final capacities remain unselected
or unimplemented as described by their current owners.

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
