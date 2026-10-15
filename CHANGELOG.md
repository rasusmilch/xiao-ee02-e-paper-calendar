# Changelog

This is the sole canonical record of meaningful project-change effects. Unreleased
summarizes the cumulative effect intended for the next merge baseline; the record
is historical evidence, not current architecture or requirements authority.

## Unreleased

- Permanently remove AI artwork generation, processing, caching and rendering,
  including OpenAI credential requirements, PNGdec and application SPIFFS use.
  Preserve retained panels, divider geometry, independent acquisition and sleep
  behavior; reserve the former artwork region for the future five-day calendar.
  Its temporarily empty state is not an independently releasable display design.
- Attempt current weather, both forecasts, and calendar acquisition independently
  on a successful data-acquisition Wi-Fi connection, so one source's failure does
  not suppress the others; preserve the current-weather-only return result.
- Validate incremental source repairs with static and focused host behavior checks,
  with complete firmware compilation and hardware validation controlled by the owner
  at milestones; compilation remains preferred and uncompiled compatibility unverified.
- Establish repository-local governance for focused stabilization work, with clear
  authority, independent review, scoped validation, durable local handoff, and
  cumulative changelog reconciliation before merge readiness. Permit controlled
  non-force publication of validated local semantic commits to the owner-designated
  feature branch, with expected-history verification and independent review before
  dependent implementation; PR management and merging remain separately authorized.
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
  library selection. Preserve the active Seeed_GFX/TFT_eSPI production implementation;
  validate isolated compilation/linkage with XIAO ESP32-S3 Plus/OPI PSRAM while
  retaining explicit hardware and production-integration evidence limits.
- Extend the locally maintained GFX2 panel with pre-initialization borrowing of
  caller-owned native T133A01 framebuffer storage, bounded initialization, ownership-
  safe cleanup and explicit unsupported-mode/mirroring failures. Preserve the
  ordinary owned-buffer path; cover borrowing, lifecycle failures and reset with
  focused host ownership tests. Production renderer migration remains pending.
- Add one capacity-parameterized application-owned PSRAM workspace with scoped
  direct scratch access and a scoped handoff to the existing EE02 framebuffer borrow.
  Preserve operation/shutdown results, prohibit memory reuse/free after uncertain
  hardware shutdown, and validate bounded prefix/tail handling and repeated healthy
  lifecycle operations. Exercise 2 MiB only as an isolated provisional evaluation;
  retain production graphics and defer the calendar pipeline.
- Add an independently allocated, capacity-parameterized PSRAM five-day snapshot
  owning normalized UTC intervals/points, civil-date all-day occurrences, complete
  UTF-8 text and source outcomes. Hide staged records until complete publication;
  roll back failed sources without damaging earlier calendars, and distinguish
  valid empty success from Pending/Incomplete. Reject invalid input and exhaustion
  without truncation or false completeness. Validate synthetic transactions and
  independence through scratch-to-framebuffer reuse; preserve production acquisition,
  keep capacities provisional and leave timezone/provider normalization unimplemented.
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
