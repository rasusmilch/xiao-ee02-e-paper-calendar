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

## Scope and development workflow

Preserve the advertised calendar, weather, and artwork capabilities and the XIAO
ESP32-S3 Plus/EE02 Arduino platform during stabilization. Follow the technical
constraints and stage dependencies in the engineering anchor. Documentation of
prototype behavior is not a production-validation claim or approval of its policy.

Work from the prepared local checkout. Do not fetch, pull, clone, query hosted Git,
push, force-update, reset, clean, or merge branches as part of this workflow.
Hosted publication, PR metadata, review state, and authorized merge belong to the
user/ChatGPT closure stage. Missing hosted access does not block local preservation.

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

## Commands and validation

Run repository commands from its root. Verified local inspection invocations:

- `git status --short` and `git ls-files`: working tree and tracked inventory.
- `git diff --check` / `git diff --cached --check`: unstaged/staged whitespace.
- `git diff --stat` / `git diff --name-status`: changed scope.
- `git check-ignore -v --no-index credentials.h`: credential exclusion.
- `git check-ignore -v --no-index docs/ENGINEERING.md`: should return status 1
  with no match; separately use `git ls-files --error-unmatch docs/ENGINEERING.md`
  to establish tracking.

Build: no repository compile command or reproducible automation exists. README
Arduino IDE settings are documented inputs, not a verified successful build.
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

The calendar selection regression runs production `doGet()` with mocked Apps Script
services and synthetic dates/events; no deployment or live access is needed:

```
node tests/google_calendar_api_test.js
node --check < google-calendar-api.gs
node --check tests/google_calendar_api_test.js
```

No general test runner or CI workflow is committed.
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
Local commits do not establish hosted publication, review, merge authorization,
or merge completion. Governance readiness remains an independent review decision.

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
