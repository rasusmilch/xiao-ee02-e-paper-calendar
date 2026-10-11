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
