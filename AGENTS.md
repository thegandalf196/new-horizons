# New Horizons repository instructions

## Scope and sources of truth

This is New Horizons, a VCMI fork for single-player play with original content.
It is not the separate Reconstruction repository. Preserve VCMI licensing,
third-party attribution, and existing gameplay outside the requested changes.

Follow the user's current task and settled decisions. The private design and
research destination is `https://github.com/thegandalf196/new-horizons-docs`.
Use a sibling checkout `../new-horizons-docs` or an explicitly configured
`NH_DOCS_ROOT` outside every public worktree. See `docs/NH_DOCUMENTATION_POLICY.md`
for document routing, research status and publication rules.

Until migration is verified, `docs/design-sources/New Horizons.md` remains the
existing canonical gameplay specification, with unintegrated amendments in
`docs/NEW_HORIZONS_PENDING_CHANGES.md`. Before feature work, also read
`docs/NEW_HORIZONS_MVP.md`, `docs/NEW_HORIZONS_DESIGN.md` and
`docs/NH_VERSION_1_0_SCOPE.md` for the existing implementation/release boundaries.
After private migration, read the private repository's index and maintained canon;
replace these transitional pointers rather than retaining parallel authorities.
Research papers inform design but do not override accepted rules. Record accepted
amendments and incorporate them into the canon. Code describes current behavior,
not proof that the intended design has been implemented correctly.

Read more specific `AGENTS.md` files for the area being changed. Read additional
reference documents when relevant to the task, rather than loading every guide.

## Current priority and work tracking

An explicit active user goal takes precedence over the default phase priorities
below. Preserve a displaced goal's progress privately and resume it only when the
user directs; do not silently return to its backlog after a task handoff.

Current phase: **Phase 2 - stabilization and integration**.
See `docs/NH_PHASE1_EXIT_AUDIT.md` for the Phase 1 exit evidence and
`docs/NH_DEVELOPMENT_PHASES.md` for detailed phase criteria.
The withdrawn 0.85 experimental preview is not a current release or a Version 1.0 release candidate.

Prioritize crashes/corruption, save/load, incorrect mechanics, action and
activation integrity, initiative, targeting, effect lifecycles, interactions,
AI use of implemented mechanics, and UI/result agreement. Avoid substantial
new mechanics until integration is stable, unless the user directs otherwise.
Defer numerical balance unless a value prevents meaningful functional testing.

Keep user priorities and execution receipts in the configured private task queue
outside tracked files and disposable build trees. Read it before selecting or
resuming backlog work; do not invent a tracked fallback if its location is
missing. Preserve unresolved requirements and record blockers, acceptance
evidence, and remaining work.
If all eligible tasks are blocked, request the missing decision or resource.

For ambiguous gameplay details, make a reasoned provisional choice and record
it in `docs/NH_PROVISIONAL_RULE_REVIEW.md` with rationale, code location, and
verification. Respect settled decisions; specification amendments still belong
in Pending Changes. Missing resources, external authority, licensing questions,
and crash/corruption risks are not gameplay choices to guess away.

## Orchestrator and standing specialist team

Use Codex's native agents; no orchestration or art plugin is required.
The main agent is the project orchestrator: own priorities, architecture, task
boundaries, integration decisions, acceptance evidence, and communication with
the user. Delegate substantive implementation, artwork, and gameplay validation
to the appropriate specialists. Handle coordination and small integration fixes
directly when appropriate; do not take over a specialist's active file ownership.

Maintain these standing roles throughout the project:

| Role | Responsibility |
| --- | --- |
| Artist | Heroes III visual style, icons, portraits, interface artwork, native-resolution previews, provenance, and selected asset packaging. |
| Game tester | Reproducible gameplay scenarios, rendered transitions, save/load, AI behavior, and observed regression evidence. |
| Gameplay engineer | Spells, Orders, Skills/perks, heroes, creatures, towns, and other specified mechanics and their focused tests. |
| Engine engineer | Simulation boundaries, serialization, shutdown/thread ownership, rendering integration, builds, and performance. |
| AI engineer | Adventure/combat decisions, use of new mechanics, legal actions, and AI evaluation scenarios. |
| Independent reviewer | Correctness, memory/state/save risks, specification agreement, and missing acceptance evidence. |
| UI developer | C++ widgets, layout, input, tooltips, and integration of approved artwork; coordinate with artist and gameplay engineer. |
| Content designer | JSON definitions, text/localization, registrations, map/scenario content, and cross-reference validation; follow authored mechanics. |

The artist and game tester remain designated members even during code-focused
work. Engage the artist for visual changes and the tester for changes affecting
observable gameplay. Specialists report to the orchestrator; they do not choose
unrelated backlog work or change project priorities on their own. Every engineer
still validates their own changes; the game tester provides independent evidence,
and the reviewer does not approve their own implementation.

Permanent means a stable role and retained project context, not continuous
inference or an idle polling loop. Reuse named specialist threads for related
assignments while available. Keep a compact handoff in the configured private
queue so a role can be restored when a thread is closed, capacity is reclaimed,
or a new main session starts. A role definition alone does not launch a session
or provide tools; report unavailable image-generation or graphical-test tools.

Give each assignment a bounded objective, file ownership, constraints, expected
result, and acceptance criteria. Avoid concurrent edits to the same files;
use isolated worktrees or private patches where appropriate. Keep frozen test
candidates unchanged. Inspect and route real follow-up work at task handoffs;
let specialists wait when no eligible task exists. Do not invent filler work.
Respect actual capacity, with no more than four allocated subagent threads
excluding the orchestrator; never interrupt unrelated user work to reclaim slots.
Count retained idle or completed worker threads toward this project budget.
Reuse them or deliberately close them before allocating replacements. A runtime
releasing a completed worker's concurrency slot does not authorize a fifth
retained project worker.
Additional research specialists may be assigned when needed.

Native specialist definitions live in `.codex/agents/`. They configure reusable
roles; the orchestrator assigns work and reuses their threads. Models, reasoning
effort, and thread limits belong in Codex configuration.
Start a specialist with its named role and a bounded handoff. Verify the executing
specialist's effective settings, not metadata copied from an earlier parent
context or a role label applied to an existing thread.
Distinguish assigned work, running work, verified results, and delivered playable
builds. Report changes, evidence, limitations, and next steps; do not treat
activity or test counts as completion.

## Code ownership and architecture

- `lib/`: mechanics, entities, bonuses, callbacks, game state, serialization,
  configuration, and pathfinding. Use existing identifiers and constants.
- `server/`: authoritative simulation and validated command processing.
- `client/`: interfaces and rendering; `clientsdl2/` and `clientsdl3/` implement
  the rendering backends.
- `AI/`: combat and adventure AI. Use callback interfaces for visible game state.
- `config/`, `Mods/new-horizons/`, and `scripts/`: data, mod content, and Lua logic.
- `launcher/`, `mapeditor/`: Qt tools; `test/`: regression and mechanic tests.

Use VCMI's internal connection and simulation thread for this single-player
fork. References to a server mean simulation authority, not a requirement for a
separate process or socket transport. Frontends submit validated commands;
never directly mutate gameplay state from UI or bypass command validation.

Follow `docs/developers/Coding_Guidelines.md` and C++20 conventions. Preserve
`DLL_LINKAGE` where required across shared-library boundaries. Consumers link
`vcmi`, the `libFacade/` aggregate, rather than `vcmiMain` directly. AI modules
are registered through `AIFactory`, not dynamically loaded libraries.

Read `docs/developers/Code_Organization.md` before implementation and review.
Keeping source files and functions focused is a standing requirement for all
future changes. Review starts above 500 file lines or 80 function lines; new or
expanded handwritten production units default to ceilings of 1,000 and 150.
Existing oversized files require bounded extraction and a shrinking baseline,
with narrowly justified exceptions under that policy. Every code handoff reports
before/after size, responsibility changes and verification; a file move alone
does not satisfy the rule.

For bonus changes, read `docs/developers/Bonus_System.md`. Preserve serialization
and save compatibility for state changes; read `docs/developers/Serialization.md`.
For combat, inspect `lib/battle/`, `server/battles/`, `client/battle/`, and relevant
Lua damage logic in `scripts/damage/damageCalculator.lua`. Use schemas and the
existing JSON loader/validator for content changes. Consult the applicable
code-structure, networking, battlefield, and logging guides as needed.

Before changing battle ownership, restart, quickload, shutdown, window clearing,
or deferred rendering, read the combat-restart guardrail in
`docs/NH_PERFORMANCE_EXPERIMENT.md`. Preserve thread ownership, shutdown, and
repaint invariants and run its regression checks and rendered-transition check.
Record recurring defects with the previous fix, protected invariant, coverage,
and any remaining acceptance evidence.

## Build, tests, and playable delivery

Use documented CMake and platform build commands. Read the relevant
`docs/developers/Building_*.md` and Conan/CMake guides for the environment in use.
Allow only one compilation invocation at a time across the team. Use at most ten
build jobs within that invocation unless the user changes that limit. For Python tools,
use the environment's verified absolute interpreter path; never bare `python`,
`python3`, or `pytest`.

Run focused checks for the changed behavior, then relevant integration and
regression suites for Phase 2 changes and checkpoints. Fix material failures;
record unrelated failures honestly. A build or source-level check does not
establish rendered behavior, working AI, or successful gameplay.

Automated graphical playtesting must use isolated background Xvfb, disposable
profiles/saves, and the maintained guarded private-launch helper. Verify both
SDL audio-driver variables are `dummy` in the actual child process. Keep runs
bounded and silent; never use the host display, input, or audio as a fallback.
If the required graphical environment is unavailable, report the verification
gap rather than claiming a pass.

The ordinary Linux launcher follows the latest validated immutable candidate.
Use `tools/ci/linux_playable_snapshot.py validate` with a reviewed, bounded
validation driver for that exact snapshot, so validation and atomic promotion
remain one operation. Compilation, checksums, or a trivial successful command
are not gameplay acceptance. Failed, changed, timed-out, or superseded
candidates leave the playable build unchanged. Preserve graphical ownership,
isolation, and cleanup gates. Explicit promotion is for reviewed rollback or
separately managed previews.
For exact-PID graphical owners incompatible with the CLI's process-group
contract, use the separately reviewed signal-free validation endpoint described
in `tools/ci/LINUX_PLAYABLE_SNAPSHOT.md`. It must retain the original in-process
ticket from before launch through actual acceptance and full postchecks; do not
import a saved receipt or replace that transaction with manual promotion.

Prefer incremental builds. Keep unique art, handoffs, saves, development
worktrees, and selected playable deliveries outside disposable build trees.
Before removing a build, verify preservation and live process, launcher, and
worktree references. Never discard unrelated changes or safely running work.

## Repository documentation discipline

Follow `docs/NH_DOCUMENTATION_POLICY.md`. Tracked prose must help a human
contributor understand the product, architecture, accepted design, tools, asset
contracts, or reproducible verification. Use one canonical home per fact and
update current sections in place rather than appending a report after every turn.

Push detailed design, philosophy, research papers and design decisions only to
`thegandalf196/new-horizons-docs`, after checking that the checkout's remote is the
intended repository and authenticated metadata reports it as private. If access
or privacy verification is unavailable, retain drafts outside the public checkout
and report the gap; do not use the public repository as a fallback. Public runtime
contracts, build guides and reproducible contributor documentation stay here.
Keep credentials and unreviewed personal records out of both repositories.

Keep chat/user-message copies, worker queues/handoffs, session/tmux IDs, wake
protocols, token/model-reset notes, raw test receipts, temporary paths, exact
art prompts/metadata, authoring masters, and rejected previews in configured
private storage outside Git. Do not create another NH_* coordination diary or
move an entire diary into docs/history to keep it public. Keep release evidence
short, portable, versioned, and tied to the behavior actually verified.

Before retiring a mixed document, preserve it privately and extract unresolved
issues, accepted decisions, technical contracts, and important regression
invariants into their maintained public homes. Update links and affected tests.
Inspect tracked/staged files: ignore patterns do not untrack existing content.
History remediation is a separate planned migration phase, after private
preservation and public-build independence are verified. Prepare the historical
path/content inventory and validate an isolated rewrite candidate before the
owner approves a concrete remote rewrite. Do not force-push as routine cleanup
or claim old public copies have been erased.

## Artwork, public identity, and distribution

Before art production, read `docs/NH_ART_DIRECTION.md` and the relevant native
asset contracts. Before UI work, read `docs/NH_HOMM3_UI_STYLE_GUIDE.md`; apply its panel-construction
and native-resolution checklist. If the guide is missing, report its absence
and use approved existing UI as reference without inventing a replacement
style policy. Mockups guide layout and interaction. Preserve
gameplay behavior unless a functional change is requested.

Artwork awaiting user review must always be copied to `$HOME/Downloads/provisory/`. Resolve `$HOME` to the current user's home directory at runtime. Create the destination directory if it does not exist.
Use a new clearly named per-artwork subfolder. Provide direct preview links and
native/enlarged previews where available; preserve previous versions. A preview
is not Final approval. Selected provisional drafts may be integrated when explicitly
authorized; keep selection, visual approval, technical verification and playable delivery distinct.

Selected runtime artwork belongs in `Mods/new-horizons/NewHorizons.nhart`.
Record selection, approval, and origin separately, including selected Cabirs,
turbaned Magi, Guild composites, and custom interface resources. Preserve local
authoring masters, inputs, alternatives, and review materials outside tracked
files; tools require configured local paths. Ordinary builds use the committed
pack without private inputs, overlays, loose fallbacks, or developer caches.
Do not rebuild it from incomplete inputs. Keep unchanged original resources
external and preserve copyright/license obligations. Verify rendered distribution.
Before removing tracked art, verify local preservation and notify affected
worktree owners about deletion on pull.

Publicly identify the owner only as `gandalf196` or `thegandalf196`. Keep personal
names, workstation usernames, absolute personal paths, private contacts,
conversation notes, and raw receipts out of tracked files and public artifacts.
Check packages, reports, screenshots, and Git author/committer identity before
publication; use the owner's alias and GitHub noreply identity for project
commits. Preserve legitimate third-party attribution. Keep raw privacy findings
and recovery material local; do not claim historical external copies were erased.
