# Documentation policy

Repository documentation explains the game, engine, tools, data formats, accepted
design decisions, and reproducible contributor workflows. It must remain useful
to a human contributor who has never seen the author's agent conversations.

## Public and private destinations

`https://github.com/thegandalf196/new-horizons` contains public source, selected
runtime resources, a player wiki, a short feature overview, implementation
contracts, build guides and reproducible runtime verification. Detailed design, philosophy,
research papers, proposed amendments and design decisions belong in the private
`https://github.com/thegandalf196/new-horizons-docs` repository.

Keep the private repository as a sibling checkout (`../new-horizons-docs`) or at
an explicit `NH_DOCS_ROOT` outside all public worktrees. Never nest its Git
checkout in this repository or publish it through a public submodule, subtree,
artifact, source bundle or copied fallback. Before pushing private documents,
verify the intended remote and authenticated repository visibility. If unavailable,
preserve drafts externally and report the access/verification gap.

The private library's `canon/game-design.md` is the active specification.
Public builds and runtime tests consume shipped configuration, not that private
document. Private fidelity checks take an explicitly configured public checkout.
Do not restore a public copy as an access fallback or create parallel authority.
Legacy mixed documents and pending amendments still require reviewed migration;
their remaining public presence does not make them a second canon.

## Canonical design and research

The private `canon/` holds governing philosophy, accepted design and current
rules. `research/` holds focused papers, such as XP progression or perk-offer
weighting, with a question, assumptions, alternatives, method, evidence,
limitations and implications. A paper may remain exploratory or inconclusive.
Distinguish analytical derivation, simulation, observed gameplay and design
preference; do not invent experiments, citations or results.

Papers cite the canon revision they examine and the code/data revision used for
measurements. A research conclusion does not amend the game automatically.
Accepted changes have a concise record in private `decisions/`, link their
supporting papers, and update the canon. Preserve superseded reasoning with
explicit links rather than a second current doctrine. Draft amendments belong
in private `proposals/`; record unresolved choices without overriding settled
user decisions.

Raw transcripts, task queues and execution logs are not research papers. Keep
ongoing operational state local; intentionally retained historical material may
enter a reviewed private archive after credential and personal-data review.
Credentials and unnecessary personal records belong in neither Git repository.

## Appropriate repository content

- Player installation, gameplay, controls, compatibility, and release notes.
  The [player wiki](wiki/README.md) is the public home for detailed player
  reference pages. Update the relevant page when its implemented rule or data
  changes; keep the short changelog as an overview and link to the wiki rather
  than maintaining duplicate reference tables.
- Public implementation rationale and contracts; detailed canonical design,
  pending amendments and design research use the private repository above.
- Architecture, invariants, serialization, public schemas/formats, and code maps.
- Portable build/test/package commands, maintained tests, and reproducible fixtures.
- Current art direction, real slot contracts, selected-asset provenance/attribution.
- Concise known issues and bounded release acceptance records tied to exact versions.
- One short AGENTS.md and reviewed native role definitions for contributor assistance.

AI authorship is not itself a reason to remove a document. A durable finding about
the game's AI, a save-corruption invariant, or an asset format is legitimate
engineering documentation. Separate facts, intended behavior, evidence, and unknowns.

## Keep outside the tracked repository

- Chat transcripts, copied user messages, instruction debates, and conversation audits.
- Worker handoffs, role queues, subagent/thread IDs, tmux pane addresses, wake/wait
  protocols, token budgets, model-reset records, and session-specific authorization.
- Per-turn narration, progress diaries, repeated test receipts, failed invocation
  logs, temporary candidate directories, and screenshot/clipboard attachment paths.
- Raw generation prompts/metadata, authoring masters, rejected art, private
  references, comparison grids, personal profiles/saves, and recovery backups.
- Plugin installers, plugin ZIPs/caches, historical AGENTS.md copies, and this
  review/setup kit's local-only material.

Use configured private storage outside the repository for these records. A local
ignored folder can be used only when project policy allows it; it is not permission
to commit the content. Do not copy old histories into docs/archive or docs/history
just to rename the problem. A public historical record must be intentionally
selected, short, sanitized, and useful to future contributors.

## Where each durable fact belongs

Use one canonical home for each fact. Put detailed mechanics in the private canon,
architecture decisions in a concise ADR or subsystem guide, regressions in tests
and a short invariant/runbook, and selected asset identity in the maintained
manifest. Keep open bug reports in the issue tracker or a compact public issue list
without copying the private task queue. Automated coverage data is acceptable if
it is current, reproducible, source-linked, and does not embed execution diaries.

AGENTS.md describes enduring rules, source pointers, and responsibilities. Models,
reasoning effort, and delegation limits belong in Codex configuration. Task-specific
assignments and running worker state belong in private coordination records.

## Updating a document

Update the current section in place. Record a changelog entry only when it helps a
release consumer; do not append a fresh paragraph for every agent action or test
run. Remove superseded active directions after preserving any useful rationale.
Avoid parallel source-of-truth documents and unbounded acceptance matrices.

A public regression record states the trigger, affected behavior, invariant, test
or reproducer, fixed version/commit, and remaining limitation. Raw logs and repeated
failed attempts stay private. A test command is evidence only for the behavior it
actually exercised, not a gameplay or platform claim beyond its scope.

Before adding tracked prose, ask: would a new contributor need this if the chat and
all agents disappeared? If not, store it privately. If yes, write a concise,
portable, technically verifiable document in the existing canonical location.

## Migrating existing histories

Preserve recoverable copies outside Git first. Extract unresolved issues, accepted
design decisions, current technical contracts, and meaningful regression lessons.
Check references in docs, tests, tools, packaging, and developer instructions before
removing a tracked file. Review and rerun affected checks. Remove current tracked
copies through an ordinary reviewed change; do not rewrite Git history as part of
routine cleanup. Historical remediation has a separate planned phase: inventory
all published references and old paths, preserve recoverable private copies,
prepare and validate an isolated candidate, then obtain approval for the concrete
remote rewrite. Coordinate other clones and review cached/PR/release copies.
Existing public history is not erased by removing a current file, and a rewrite
cannot recall copies already downloaded.

Ignore rules prevent future untracked additions but do not untrack existing files.
Review git status and staged diffs before publication. Do not blanket-delete docs
or suppress tests just because their filenames contain AI, tester, or review.
