# Repository contract

## Reading order and authority

Before editing, inspect the current checkout, working tree, applicable instructions,
and relevant source. Read:

1. This file: development/agent operations, command status, review, and receipts.
2. [README.md](README.md): project intent, advertised capabilities, configuration,
   and operator setup/deployment/recovery guidance.
3. Relevant sections of [docs/ENGINEERING.md](docs/ENGINEERING.md): architecture,
   hardware, technical constraints, security, testing, limitations, roadmap, and
   Open decisions. Its explicitly historical sections own decision provenance and
   validation evidence, not current requirements.
4. [CHANGELOG.md](CHANGELOG.md): the sole canonical changelog and its Unreleased
   effects; then the actual code/interfaces affected by the task.

Current source establishes current behavior. Explicit user-approved intent
establishes desired behavior. The anchors describe that distinction; code,
comments, tests, defaults, prior tasks, receipts, and Git history do not establish
product approval merely by existing. Recommendations remain proposals and Open
decisions remain Open. Preserve material provenance and applicability; when a
decision changes, update its current owner and preserve truthful historical
supersession separately. Resolve material conflicts rather than picking the newest
wording or implementation. No private guide or template is needed to follow this
contract.

Every future task involving graphics, PSRAM, calendar transport, ICS parsing,
recurrence, event storage or wake-cycle sequencing must consult the
[selected GFX2, PSRAM and streaming-calendar architecture](docs/ENGINEERING.md#selected-gfx2-psram-and-streaming-calendar-architecture)
before planning or implementation. It owns the application-owned workspace,
independent snapshot, transactional source processing and framebuffer-borrow
contract. Selected direction is not current implementation or validated memory
capacity. Contradictory source, test or hardware evidence requires explicit review
and adjudication before changing the architecture; never silently substitute a
different ownership, processing or allocation model.

## Scope and development workflow

Retain the XIAO ESP32-S3 Plus/EE02 Arduino platform and the calendar, weather,
forecast, and battery-monitoring capabilities. The approved target replaces the
artwork area with a rolling five-day calendar and retires all artwork-only code,
credentials, assets, dependencies and dead paths. Read the target requirements,
current-source observations, risks and staged work in the engineering anchor;
do not treat a design decision as implemented or use the redesign to bypass
stabilization/validation. Existing prototype behavior is evidence, not approval
of continuing retired functionality.

Work from the prepared local checkout. Remote Git operations are restricted to
the authorized feature-branch publication and verification policy below unless
separately authorized. Missing hosted access does not block local preservation.

Use the lifecycle: Plan → ChatGPT Review → Execute → Validate → Commit → Publish
→ ChatGPT Review. Each published semantic unit must receive independent ChatGPT
execution and intent review before the next dependent implementation unit is
authorized. The selected redesign workstream uses one feature branch,
`feature/ee02-week-view`, and one eventual draft PR for the complete redesign;
the branch designation is owner-approved, while PR creation remains separately
authorized.

Execute one authorized, independently reviewable semantic outcome at a time.
Split unrelated outcomes even when they share a file; do not split mechanically
by file or line count. Inspect source and affected authority before changes, check
plan freshness, and protect unrelated user work. Do not install/upgrade dependencies,
deploy services, invoke live APIs with project credentials, or flash hardware without
specific authorization. A roadmap is not execution permission.

Prefer the smallest design satisfying a selected requirement. Before adding durable
state, persistence, abstractions, or duplicate representations, compare the closest
simpler design and explain the actual independent behavior needed. Speculative
flexibility alone is insufficient. Keep hardware-independent logic separation small.

Stop for review when required source/authority is missing, contradictory, or stale;
source changes invalidate the approved scope; a repair would entrench a flawed
model; or safe execution requires selecting a material Open product/technical
policy. Do not ask the owner to select routine implementation details. Preserve
completed work and report unavailable checks rather than inventing evidence.

## Authorized feature-branch publication

Codex may publish a completed, validated and locally committed authorized semantic
unit to the feature branch designated by the owner-approved workstream or task.
This is standing permission within that scope; a separate publication exception
is not needed for each unit. Do not infer authorization from an arbitrary branch
name. Local durable preservation remains mandatory before publication.

Before pushing, verify repository identity and sanitized origin fetch/push
destinations, a clean working tree and index, the intended commit and feature
branch, commit ancestry, and the expected remote branch state. Establish the
expected predecessor from the approved task or last verified workstream publication,
not from whatever SHA the remote happens to report. Inspect the commit range to
ensure it contains only the approved semantic unit, without additional unreviewed
work outside its scope.

Use narrowly scoped `git ls-remote` queries for pre-push and post-push verification.
The remote branch HEAD must equal the expected predecessor, which must be an
ancestor of the commit being published, or already equal that exact commit. In
the latter case, verify and report publication as already complete without pushing.
If the destination is absent, has advanced unexpectedly, diverged, or cannot be
verified safely, stop without pushing and report the discrepancy. Do not silently
reconcile history or automatically fetch, pull, merge, rebase, reset or change
branches to resolve it.

Publish with an ordinary non-force Git push using an explicit refspec from the
intended local commit to the designated feature branch. Never force-push, overwrite
remote history or push directly to `main`. After publication, verify the remote
branch SHA equals the intended commit and report the result accurately; preserve
the local branch, HEAD, index and working tree. A successful push establishes
publication, not independent review, merge authorization or merge readiness.

This permission does not authorize tags, unrelated branches or repositories,
other hosted queries, PR creation/merging, PR metadata changes, or changes to
hosted review state. Do not automatically fetch, pull, clone, modify unrelated
repositories, perform destructive Git operations, or force-update any reference.
Service deployment and hardware flashing remain separately authorized activities.
Hosted PR creation and management, review, integration approval and merging
remain separately authorized user/ChatGPT activities.

## Commands and validation

Run repository commands from its root. Verified local inspection invocations:

- `git status --short` and `git ls-files`: working tree and tracked inventory.
- `git diff --check` / `git diff --cached --check`: unstaged/staged whitespace.
- `git diff --stat` / `git diff --name-status`: changed scope.
- `git check-ignore -v --no-index credentials.h`: credential exclusion.
- `git check-ignore -v --no-index docs/ENGINEERING.md`: should return status 1
  with no match; separately use `git ls-files --error-unmatch docs/ENGINEERING.md`
  to establish tracking.

Build: no verified production firmware compile command or reproducible automation
exists. README Arduino IDE settings are documented inputs, not a verified successful
build.
The isolated GFX2 compile/link fixture and explicit library-selection template are
documented in the [engineering baseline](docs/ENGINEERING.md#vendored-gfx2-dependency-baseline);
its isolated owned/borrowed target compile/link has passed with the documented
toolchain, not a substitute for production or hardware validation.
Use static checks and applicable lightweight host behavior tests during incremental
development. Complete firmware compilation and hardware validation are
owner-controlled milestone activities; compilation is preferred but is not a
prerequisite for initial source repairs. Uncompiled changes do not establish
firmware compatibility. No CI or hosted build infrastructure is required.
Tests: the focused acquisition regression exercises production orchestration:

```
g++ -std=c++11 -Wall -Wextra -Werror -pedantic tests/data_acquisition_test.cpp -o /tmp/ee02-data-acquisition-test
/tmp/ee02-data-acquisition-test
```

No general test runner or CI workflow is committed.
The focused GFX2 ownership regression compiles the actual modified panel and reset
implementation with minimal test-local hardware stand-ins:

```
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -Itests/gfx2_host tests/gfx2_framebuffer_test.cpp third_party/Seeed_GFX2/src/panel/Panel_EPaper.cpp third_party/Seeed_GFX2/src/runtime/DisplayInstance.cpp third_party/Seeed_GFX2/src/driver/epaper/SeeedEPaperProfiles.cpp -Wl,--wrap=malloc,--wrap=free -o /tmp/ee02-gfx2-framebuffer-test
/tmp/ee02-gfx2-framebuffer-test
```

Adding `-fsanitize=address,undefined -fno-omit-frame-pointer` to this host compile
also passed. It validates software ownership/lifetime, not electrical display behavior.

The scoped application workspace regression uses the actual owner/panel/reset and
checks independent snapshot survival through scratch poisoning/framebuffer reuse,
with minimal test-local PSRAM, facade and factory stand-ins (no production injection):

```
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -Itests/gfx2_host -Ithird_party/Seeed_GFX2/src tests/psram_workspace_test.cpp third_party/Seeed_GFX2/src/panel/Panel_EPaper.cpp third_party/Seeed_GFX2/src/runtime/DisplayInstance.cpp third_party/Seeed_GFX2/src/driver/epaper/SeeedEPaperProfiles.cpp -Wl,--wrap=malloc,--wrap=free -o /tmp/ee02-psram-workspace-test
/tmp/ee02-psram-workspace-test
```

Its ASan/UBSan variant uses the same sanitizer flags above. Target compilation of
the isolated fixture uses real vendored GFX2, not these host stand-ins; runtime
PSRAM/stack/heap and electrical behavior still require physical measurements.

The independent normalized snapshot regression uses its actual implementation and
a minimal guarded PSRAM stand-in, with no provider, parser or timezone resolver:

```
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -Itests/gfx2_host tests/five_day_snapshot_test.cpp -Wl,--wrap=free -o /tmp/ee02-five-day-snapshot-test
/tmp/ee02-five-day-snapshot-test
```

Its ASan/UBSan variant uses the same sanitizer flags above. These tests cover supplied
window semantics, ownership and source transactions, not real calendar completeness,
timezone resolution or physical PSRAM capacity.

The standalone raw HTTP/ICS-unfolding regression uses allocation-free production
headers with synthetic byte reads, a monotonic clock and guarded caller storage:

```
g++ -std=c++11 -Wall -Wextra -Werror -pedantic tests/ics_ingestion_test.cpp -o /tmp/ee02-ics-ingestion-test
/tmp/ee02-ics-ingestion-test
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer tests/ics_ingestion_test.cpp -o /tmp/ee02-ics-ingestion-sanitized
/tmp/ee02-ics-ingestion-sanitized
```

The existing workspace command also exercises scratch-only ingestion and
poison/reuse, without new snapshot publication or graphics integration. These
checks establish supported framing/unfolding behavior, not valid calendar events,
secure redirects, actual TLS/socket binding or target firmware compatibility.

The bounded property/component regression exercises the actual standalone parser
and existing ingestion bridge with guarded caller storage and no per-property
allocation, semantic normalizer, live feeds or snapshot publication:

```
g++ -std=c++11 -Wall -Wextra -Werror -pedantic tests/ics_structure_test.cpp -o /tmp/ee02-ics-structure-test
/tmp/ee02-ics-structure-test
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer tests/ics_structure_test.cpp -o /tmp/ee02-ics-structure-sanitized
/tmp/ee02-ics-structure-sanitized
```

The workspace command also covers framing/structure within one `withScratch()`
scope, disjoint slices, cleanup and poisoning. The isolated EE02 fixture now
compiles synthetic structural parsing; production and hardware remain unverified.

The selective source-stage regression composes actual ingestion/structure with
borrowed descriptor/pool storage, explicit projections and review budgets:

```
g++ -std=c++11 -Wall -Wextra -Werror -pedantic tests/ics_source_stage_test.cpp -o /tmp/ee02-ics-source-stage-test
/tmp/ee02-ics-source-stage-test
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer tests/ics_source_stage_test.cpp -o /tmp/ee02-ics-source-stage-sanitized
/tmp/ee02-ics-source-stage-sanitized
```

The workspace command also covers staged-data independence from transient parser
buffers, opaque accounting, release and poisoning inside one scratch lifetime.
These checks establish retention, not semantic calendar or snapshot completeness.

Documentation: no dedicated lint/link-check command is committed; inspect Markdown
references and affected claims directly, using available tools.
Review: no automated review command exists; independent review inspects source/diff.
See the engineering anchor for validation strategy and baseline limitations.

Choose checks from the actual delta, credible failure modes, and affected guarantees;
known commands are not an automatic checklist for every task. Documentation-only
work needs semantic/link/ignore/scope checks. Behavioral changes need relevant
regressions and build evidence when available; integrated work may need broader
checks. Compilation does not prove hardware operation. Host tests do not prove
electrical, display, persistence, or wake behavior.

Report current execution, historical evidence, and carried-forward evidence with
an applicability judgment separately. State the covered scope and material checks
not run/unavailable/deferred, with reasons. Receipts and passing checks are claims
and scoped evidence, not product correctness or readiness by themselves.

## Documentation and changelog reconciliation

After the final substantive delta and before completion or durable preservation,
search directly affected current-state owners for stale claims using bounded terms
derived from the change. Go beyond sentences/files named in the task, inspect hits
in context, and coherently rewrite affected authority in place. Remove contradictory
active wording; leave wholly accurate wording unchanged. Do not append override
layers or conduct unrelated repository-wide cleanup. Report protected/out-of-scope
defects; do not silently expand the task. If reconciliation requires a new material
decision, stop for adjudication. Known stale authority cannot be deferred. Preserve
historical decision/evidence records as history.

Only independent execution review may permit one-hop clerical carry-forward for
one or a few review-discovered wording misses in an otherwise accepted intermediate
unit. Final source, selected decisions and non-conflicting authority must predetermine
the exact correction. It must be non-executable/non-generated/non-parsed, involve
no configuration, build, deployment, runtime, migration or persistence change, no
new durable decision or Open interpretation, no altered next-task contract, and no
material security/operator/recovery risk. Broad drift, unclear ownership, behavioral
defects and validation gaps are ineligible. A real immediately next same-stack task
must exist, the correction must be distinguishable from its new delta, and the stack
must not be approaching closure. Review records the owner, stale claim, exact truth
and why these conditions hold. Its next action generates that approved descendant
with the correction as mandatory preflight. The descendant applies it or verifies
it already satisfied before substantive work; ambiguity stops execution. Descendant
review checks the correction separately from the new work. An omitted, incorrect,
broadened or still-stale correction is NO-GO; it cannot be carried again or remain
at checkpoint/integrated-validation, changelog-finalization or merge-readiness
closure. No registry or persistent carry-forward machinery is needed.

For changed code, document meaningful contracts, units, failure semantics, and
hardware assumptions; distinguish assumptions from measurements. Reconcile affected
README/engineering descriptions without mandating comment volume or documenting
unapproved behavior as a requirement.

Root CHANGELOG.md records meaningful cumulative merge-facing effects, not task,
commit, repair, filename, or PR chronology, and is not current architecture or
requirement authority. Intermediate tasks need not edit it when Unreleased remains
accurate; early cumulative updates are optional. Before merge readiness, reconcile
the complete workstream against Unreleased and final approved/current authority.
Changelog deferral never postpones current-state documentation repair.

## Security and durable handoff

Keep credentials.h ignored. Never commit, print, or copy live secrets, secret
inventories, or sensitive calendar/API logs into receipts. Inspect only necessary
non-secret configuration; use the engineering security boundary and operator
instructions. Do not create substitute credentials to force a check.

A completed tracked semantic execution unit that passes its applicable checks must
be preserved durably before exit, normally by a local commit containing only that
unit. Read-only/no-change tasks need no commit. If preservation fails, retain the
validated working tree and report the exact blocker; do not discard/recreate it or
describe uncommitted work as durable. Check and report final working-tree state.

Require independent ChatGPT execution and intent review of planning/execution
results. Integrated validation of the complete workstream, independent review GO,
and cumulative changelog reconciliation are required before merge readiness.
Local commits establish durability; feature-branch publication follows the policy
above and does not establish independent review, merge authorization or merge
completion. Governance readiness remains an independent review decision.

## Compact receipt contract

Aim for approximately 250–400 words; expand only for material failures or evidence
that reviewers need. Use exactly one outer plain-text fence: opening delimiter
`~~~text`, closing delimiter `~~~`, with no nested fences or receipt text outside.
Inside, open with a line `===`, then `Receipt — <task>`, then `===`; close with
`===`, `End Receipt — <task>`, `===` using the same task title.

Include material changes/affected files; commands actually executed and concise
results; applicable tests/checks not run and why; documentation/changelog effects
and reconciliation scope; remaining risks/unverified claims and evidence limits;
local working-tree/durability status; and exactly one next action. Longer evidence
belongs in appropriate artifacts or explicitly historical validation records, not
automatically in the receipt. Do not claim readiness merely from receipt compliance.
