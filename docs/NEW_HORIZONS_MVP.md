# Heroes III: New Horizons — MVP

## Product contract

Working title: Heroes III: New Horizons. This is a VCMI-derived, GPL-covered project, separate from the Reconstruction repository. Preserve upstream license notices and attribution. Naming/public distribution rights require separate review.

Deliver a fully playable ordinary single-player game using purchaser-supplied original Heroes III Complete assets. No original assets or executables may be committed, modified, or redistributed.

Single-player must use one game process, with no separate vcmiserver child and no loopback TCP/UDP connection or listener. Keep authoritative simulation, AI, commands and presentation ordering intact. An in-process simulation thread is permitted. Current upstream ServerThreadRunner and createInternalConnection are the first implementation candidates, not a mandate to replace working logic.

The single-player frontend must read as a local game: Main Menu -> New Game -> Single Scenario -> map/options -> Begin -> Adventure. Load Game must likewise reach saves without hosting/address/port/connect decisions. Internal lobby/session classes may remain, but do not expose networking terminology or a separate join/host interaction in ordinary single-player. Preserve necessary loading progress, cancellation and actionable errors; no redesign of game rules is required.

Players receive one curated original-content edition: no optional third-party mod installation/selection workflow. Preserve internal content/dependency loading and upstream-required engine resources for compatibility and future curated additions. Do not claim that GPL recipients are unable to modify the source.

## First acceptance gate

Build and run the ordinary installed-asset journey: menu -> scenario selection -> start -> hero selection and multi-step movement -> resource pickup -> Town/build/recruit -> AI and at least seven turns -> combat and spell -> save -> quit -> reload -> continued play -> victory/defeat. Verify no separate server process or single-player socket transport. Source/tests alone do not establish graphical acceptance.

Before execution, record platform/build/source identity and asset location without committing proprietary content. Existing physical-desktop/graphical automation restrictions are not lifted by this implementation request; obtain a separate explicit bounded run authorization. Native Linux first; preserve Windows buildability.

## Current testing authorization and continuation

The user explicitly authorized as much testing as needed, fixing actual failures,
and using the agents. This lifts the earlier new-project graphical-run hold.
Use one designated Tester and private background Xvfb/XTest only: no host focus or
pointer interaction, portal, visible fallback, or simultaneous original execution.
Original assets remain external/read-only. No package installation is inferred.
Keep individual runs bounded, retain concise results and a few useful screenshots,
and stop/rebuild on a concrete defect rather than repeat unchanged failures.

Current shared-tree roles: Content transitions to sole graphical Tester (no
product edits while executing); Runtime owns runtime fixes; Frontend owns UI
fixes; Build owns serialized builds. The integrator owns commits/pushes. Freeze
candidate bytes during each run and communicate failures directly to the owner.
Follow one failed run through fix, rebuild and retest; do not stop at reports.

After the complete MVP gate passes, continue implementing evidenced original
Heroes III features/behavior missing from this fork. Use current VCMI source,
upstream issue/test evidence and independently corroborated original references.
Distinguish an omission from an intentional difference or an already-fixed issue.
Reconstruction is reference evidence, not automatically validated implementation.
Record uncertainty and add focused regression tests before importing behavior.

## Delivery sequence

1. Establish upstream build and dependency baseline; no new engine/framework.
2. Trace and enforce the existing internal single-player transport; test start/load/shutdown and failure paths.
3. Curate original content and hide unsupported mod/multiplayer entry points without deleting engine content mechanisms.
4. Build one candidate and perform independently authorized normal-input play.
5. Fix actual divergences; publish a known-tested preview with explicit limitations.

The Reconstruction tree is read-only reference evidence. Do not automatically import its code. Any reuse requires original-behavior corroboration and provenance/license review. Do not continue the old reconstruction implementation program as part of this project.

## Ownership

- W1: server/simulation lifetime and existing internal single-player transport. Coordinate shared interface with W2.
- W2: client single-player selection/connection flow and usable frontend. No gameplay rule rewrite.
- W3: original-content/dependency inventory, curated-content policy; initially read-only proposal, then assigned non-overlapping config changes.
- W4: save/load/campaign continuity regression inspection; preserve existing behavior, no speculative rewrite.
- W5: independently verify no network/child-process path in single-player, propose focused negative tests; no overlapping W1 edits.
- W6: CMake/dependency/build/package baseline, reuse one build root. Own build-file edits.
- TESTER: independent normal-input acceptance plan and later authorized execution; no product source.
- Scholar/Oracle: targeted existing-code/evidence/provenance review only when requested; no broad repeated study.
- Taskmaster/Orchestrator: assign concrete non-overlapping work and integrate; no migration of the old watchdog project or receipt bureaucracy.

Use new-project sessions rooted here with these instructions, not old Reconstruction sessions. Preserve old worker changes at safe checkpoints. Keep one shared clean integration branch and at most necessary worker worktrees; never reset others' work.

## Git

Initial branch: definitive-mvp, based on upstream develop bc0fc2a9f83c9caf0d90575e38ad4b7008b2764a. Legacy develop and experimental branches are preserved.

Commit tested coherent changes and push definitive-mvp to origin https://github.com/thegandalf196/vcmi.git. Use author AND committer thegandalf196 <thegandalf196@users.noreply.github.com>. Scan staged diffs for credentials, personal data, assets and generated files. No force pushes, experimental-branch changes or default-branch changes. Source commits do not imply playable acceptance.
