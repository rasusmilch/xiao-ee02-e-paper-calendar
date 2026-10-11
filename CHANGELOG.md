# Changelog

This is the sole canonical record of meaningful project-change effects. Unreleased
summarizes the cumulative effect intended for the next merge baseline; the record
is historical evidence, not current architecture or requirements authority.

## Unreleased

- Validate the complete calendar response before replacing the volatile event
  snapshot: valid empty arrays clear it, successful responses clear unused slots,
  and error/malformed responses fail without partial replacement. Preserve event
  limits and safe bounded string copying without selecting persistent retention.
- Select the earliest eight daily events globally across all accessible calendars
  in displayed start-time order, preserving source encounter order for ties and
  existing all-day formatting; the unchanged six-event firmware capacity receives
  the earliest six of that selection.
- Attempt current weather, both forecasts, and calendar acquisition independently
  on a successful data-acquisition Wi-Fi connection, so one source's failure does
  not suppress the others; preserve the current-weather-only return result.
- Validate incremental source repairs with static and focused host behavior checks,
  with complete firmware compilation and hardware validation controlled by the owner
  at milestones; compilation remains preferred and uncompiled compatibility unverified.
- Establish repository-local governance for focused stabilization work, with clear
  authority, independent review, scoped validation, durable local handoff, and
  cumulative changelog reconciliation before merge readiness.
- Clarify prototype configuration, service/data boundaries, unresolved dependency
  compatibility, and build/hardware evidence limits so developers can distinguish
  approved direction from implementation observations and unselected policies.
