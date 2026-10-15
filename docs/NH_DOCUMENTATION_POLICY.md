# Documentation policy

Repository documentation explains the game, engine, tools, data formats, accepted
design decisions, and reproducible contributor workflows. It must remain useful
to a human contributor who has never seen the author's agent conversations.

## Appropriate repository content

- Player installation, gameplay, controls, compatibility, and release notes.
- Canonical design, short pending amendments, and decisions with lasting rationale.
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

Use one canonical home for each fact. Put mechanics in the authored design,
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
routine cleanup. Existing public history is not erased by removing a current file.

Ignore rules prevent future untracked additions but do not untrack existing files.
Review git status and staged diffs before publication. Do not blanket-delete docs
or suppress tests just because their filenames contain AI, tester, or review.
