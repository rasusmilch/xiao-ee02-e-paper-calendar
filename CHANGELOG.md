# Changelog

This is the sole canonical record of meaningful project-change effects. Unreleased
summarizes the cumulative effect intended for the next merge baseline; the record
is historical evidence, not current architecture or requirements authority.

## Unreleased

- Permanently remove AI artwork generation, processing, caching and rendering,
  including OpenAI credential requirements, PNGdec and application SPIFFS use.
  Replace the former artwork region with the rolling five-day calendar while
  retaining weather/forecasts, today details and battery capability. Keep final
  geometry, agenda/grid and historical monthly-grid retention Open.
- Make the root sketch the canonical GFX2 application by promoting the reviewed
  integrated path and retiring its duplicate live preview entry point/template.
  Use one Wi-Fi session/fresh SNTP observation per configured refresh, independent weather and
  sequential authenticated ICS calendars, one battery sample, one independent
  snapshot and one borrowed-framebuffer dashboard refresh. Load authoritative versioned
  NVS user settings with whole-candidate validation, deterministic checked blob
  replacement, unchanged-write avoidance, explicit settings-only clear and a secret-safe
  presentation/Keep-Replace-Clear boundary (full calendar URLs remain visible). Keep
  CA material/evaluation limits build-only, with no automatic import, default settings
  or routine flash churn. Provide a bounded local settings page and Missing-only open
  first-boot AP with secret-safe forms, complete URL presentation, CSRF/Host/Origin
  checks, monotonic idle/absolute/request limits, verified Save/Finish/restart and
  checked build-only setup-retry sleep; retain explicit LAN-session reuse without
  taking ownership of the caller network. Keep physical recovery/reset and RTC
  policies deferred. Add checked explicit timer deep sleep after
  verified network/panel quiescence and legal storage release; refuse sleep for
  Retained/unsafe shutdown or timer rejection. Remove active Apps Script/TFT_eSPI,
  six-event/month-grid and legacy night-dead-zone execution without a fallback.
  Preserve ignored old user configuration and inactive source as reference.
- Authenticate the three production OpenWeatherMap HTTPS requests with explicit
  caller CA material, a fresh per-boot SNTP notification gate and original-hostname
  verification. Reject missing trust, redirects and transfer failures without insecure
  fallback; preserve independent weather/forecast/calendar outcomes, parsing and
  observation ownership. Retire insecure Apps Script from active execution; leave certificate-update policy,
  final RTC trust and target/hardware verification unresolved.
- Validate incremental source repairs with static and focused host behavior checks,
  with complete firmware compilation and hardware validation controlled by the owner
  at milestones; compilation remains preferred and uncompiled compatibility unverified.
- Establish repository-local governance for focused stabilization work, with clear
  authority, independent review, scoped validation, durable local handoff, and
  cumulative changelog reconciliation before merge readiness. Require local commit
  and verified safe non-force publication to the owner-designated feature branch
  in the same execution run for every completed, validated, authorized tracked
  execution unit, without separate approval for each qualifying push. Distinguish
  that standing authorization from generic hosted-operation restrictions and resolve
  genuine task conflicts before execution. Preserve expected-history checks, local
  commits when publication is blocked, and independent review before dependent
  implementation; read-only tasks remain exempt, and PR management and merging
  remain separately authorized.
- Clarify prototype configuration, service/data boundaries, unresolved dependency
  compatibility, and build/hardware evidence limits so developers can distinguish
  approved direction from implementation observations and unselected policies.
- Record the selected Seeed_GFX2 external-framebuffer ownership extension and
  application-owned shared PSRAM architecture, with sequential streaming ICS,
  an independent five-day snapshot and transactional per-calendar publication.
  Require future related work to consult the canonical contract and adjudicate
  contradictory evidence; keep sizing budgets provisional and application integration,
  runtime memory reliability and hardware behavior unverified.
- Add one commit-pinned Seeed_GFX2 vendor baseline with upstream
  licensing and an isolated EE02/T133A01 compile/link fixture using explicit Arduino
  library selection. Retain the superseded Seeed_GFX/TFT_eSPI files only as inactive reference;
  validate isolated compilation/linkage with XIAO ESP32-S3 Plus/OPI PSRAM while
  retaining explicit hardware and production-integration evidence limits.
- Extend the locally maintained GFX2 panel with pre-initialization borrowing of
  caller-owned native T133A01 framebuffer storage, bounded initialization, ownership-
  safe cleanup and explicit unsupported-mode/mirroring failures. Preserve the
  ordinary owned-buffer path; cover borrowing, lifecycle failures and reset with
  focused host ownership tests and canonical root composition; target/hardware validation remains pending.
- Add one capacity-parameterized application-owned PSRAM workspace with scoped
  direct scratch access and a scoped handoff to the existing EE02 framebuffer borrow.
  Preserve operation/shutdown results, prohibit memory reuse/free after uncertain
  hardware shutdown, and validate bounded prefix/tail handling and repeated healthy
  lifecycle operations. Exercise 2 MiB only as an isolated provisional evaluation;
  use the same workspace in the canonical root while deferring measured operating capacities.
- Add an independently allocated, capacity-parameterized PSRAM five-day snapshot
  owning normalized UTC intervals/points, civil-date all-day occurrences, complete
  UTF-8 text and source outcomes. Hide staged records until complete publication;
  roll back failed sources without damaging earlier calendars, and distinguish
  valid empty success from Pending/Incomplete. Reject invalid input and exhaustion
  without truncation or false completeness. Validate synthetic transactions and
  independence through scratch-to-framebuffer reuse and canonical root acquisition;
  keep capacities provisional and leave timezone/provider normalization unimplemented.
- Connect staged ICS to transactional five-day snapshot publication for a closed
  nonrecurring VEVENT profile: exact decoded UID uniqueness, UTC timestamps,
  civil-date all-day ranges and decoded titles. Validate every event, including
  outside-window input; admit passive METHOD:PUBLISH/X-WR metadata, validated ignored
  administrative/display-irrelevant fields, confirmed/public events, declared
  attachment/image projections, direct alarms and locally validated unused timezone
  definitions. Keep floating/TZID times, recurrence, cancellation/revision selection,
  privacy/tentative presentation and unknown event/root semantics unsupported; roll
  back failed sources without harming earlier calendars. Reuse
  two bounded caller-owned decoding buffers without another event store. Drive the
  deterministic EE02 preview through actual HTTP/ICS ingestion and normalization;
  retain explicit incomplete-source warnings, independent scratch/snapshot ownership,
  deferred coordinated device integration and unverified target/hardware compatibility.
- Add a reusable read-only five-day snapshot renderer and dedicated EE02 example
  preview using the application workspace's borrowed GFX2 framebuffer. Show timed,
  point, all-day and spanning events with explicit local-time conversion, stable
  bounded ordering, source-status warnings and visible text/row overflow. Keep the
  five-column agenda provisional. Build rolling local five-day windows from explicit
  trusted UTC and configured POSIX TZ using independently resolved local midnights,
  variable-length days, earliest repeated midnight and atomic rejection of missing
  midnight/context. Generate and verify the deterministic preview's original window;
  compose it in the canonical root and defer
  broader normalization, final presentation, target compatibility and physical validation.
- Compose the five-day calendar, retained weather/short and daily forecasts,
  today's snapshot events and supplied battery observation through one read-only
  bounded GFX2 dashboard renderer. Share existing weather records without changing
  acquisition behavior; separate existing weather acquisition and battery measurement
  from legacy graphics for direct observation reuse. Distinguish unavailable values
  and presentation overflow, and exercise
  the full deterministic ICS-driven screen before one borrowed-framebuffer refresh.
  Leave final RTC/configuration/recovery, month-grid retention, final layout and hardware
  validation separate.
- Add a reusable single-source authenticated HTTPS acquisition path streaming raw
  Content-Length/chunked responses through existing ingestion, staging and the
  nonrecurring snapshot producer. Require explicit Wi-Fi, trusted time and CA trust;
  retain the hostname for TLS verification/SNI after one synchronous DNS lookup.
  Preserve finite socket/handshake/inactivity controls and all input capacities,
  allow slow successful work without an overall duration cutoff, and expose the
  ingestion total-time opt-out while retaining positive-budget behavior. Refuse
  redirects safely, preserve source rollback and independent snapshot ownership,
  and compose it in the root while leaving target/physical validation deferred.
- Compose trusted rolling local dates, enabled-source compaction, one independent
  snapshot and sequential authenticated HTTPS/ICS processing into a reusable
  multi-calendar refresh. Preserve successful calendars across source failures,
  reconcile selected source outcomes and distinguish interrupted refreshes, empty
  schedules and no calendar selection. Reuse that integrated development path as
  the canonical root, with separate caller-provided weather/calendar trust and
  request inputs, network shutdown before graphics, checked timer sleep and unchanged
  Retained safeguards. Keep the deterministic preview independent. Fresh NTP each
  wake and explicit refresh seconds are conservative interim parameters, not final
  RTC/daily-sync/default/energy policy. Live services, root target compilation,
  physical behavior and release readiness remain unverified.
- Add bounded standalone raw HTTP response framing and strict RFC 5545 logical-line
  unfolding in borrowed scratch storage. Require exact supported body completion,
  preserve octets across arbitrary read boundaries and fail explicitly on malformed,
  incomplete, unsupported or over-budget input. Return redirect-header handoffs
  without following them; keep provisional lines separate from semantic success.
  Validate synthetic fragmentation/failure and workspace lifetime behavior without
  changing production acquisition, snapshot publication or provider/TLS policy.
- Validate complete unfolded ICS properties and RFC 5545/RFC 9073 component
  structure in bounded caller-owned scratch with transient contextual callbacks.
  Preserve encoded parameters and full value octets; distinguish unsupported
  extensions, malformed nesting and exhaustion without false calendar completeness.
  Cover guarded workspace use and isolated EE02 compilation, retaining separate
  production staging, semantic normalization and snapshot publication boundaries.
- Add selective source-local ICS dependency staging in borrowed workspace tables
  and a bounded value/parameter pool, preserving full identities, recurrence,
  cancellation/revision and timezone evidence without semantic interpretation.
  Record explicit non-event/value omissions; project structurally accepted opaque
  components out without discarding relevant unknown event/timezone properties.
  Enforce review/storage limits, source completion and transient-view independence;
  retain separate normalization, snapshot-publication and production-integration work.
- Decode retained ICS primitive values without allocation or source mutation,
  preserving calendar fields, duration components and unresolved temporal context.
  Add bounded scalar TEXT/RFC 6868 writers, property-local VALUE/TZID checks and
  temporal-list iteration with explicit errors and complete-consumption requirements.
  Preserve omissions/review obligations and defer RECUR, complete component/source
  validation, timezone conversion, normalization and snapshot publication.
- Validate selected VEVENT-local invariants in bounded read-only staged data:
  component/property association, singleton/required fields, METHOD-presence context,
  endpoint/duration exclusion, selected typed values and decidable relationships.
  Return deterministic bounded failures and explicit unresolved obligations without
  event records, recurrence/revision resolution, source completeness or publication.
- Validate selected VTIMEZONE and STANDARD/DAYLIGHT local schemas in read-only
  staged data, with required fields, singleton cardinality, typed local onset/UTC
  offsets, complete bounded RDATE lists and deterministic failures. Preserve
  timezone identity, transition, recurrence, EXDATE and extension obligations;
  add no resolution, conversion, source acceptance or snapshot publication.
