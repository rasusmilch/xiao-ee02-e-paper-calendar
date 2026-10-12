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
  focused host ownership tests. No application workspace or renderer migration occurs.
