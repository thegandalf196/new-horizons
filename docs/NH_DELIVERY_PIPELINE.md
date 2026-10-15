# Development, validation and delivery

## Candidate contract

Keep implementation separate from a frozen validation candidate. A candidate pins
source/diff identity, binaries, configuration, artwork, launcher, platform and
known limits. Fixes create a new identity; never replace bytes under an active
test. Future work may continue in isolated worktrees outside disposable build
directories without changing frozen sources, resources or profiles.

Build directories are disposable. Preserve unique masters, handoffs, saves,
worktrees, raw receipts and recovery copies in configured local storage outside
them. Reuse an incremental toolchain rather than accumulating debug builds.
Before cleanup, check live processes, worktrees and launcher references.
Use at most ten compile jobs and one compile invocation at a time.

## Portable runbook

Use the platform build guides under `docs/developers/` and the maintained
`tools/README.new-horizons.md` for commands and dependencies. Verify the actual
interpreter path before Python tooling; examples use an explicitly configured
verified interpreter, not an unqualified command resolved from ambient PATH.
Do not change dependencies or global configuration as an incidental test step.

Ordinary builds consume the committed NewHorizons.nhart without private authoring
inputs. Pack reconstruction requires explicit complete inputs, staged output and
baseline-preservation checks; keep a recoverable baseline and do not publish partial
writes. Matching source, engine resources, dependencies, notices and runtime DLL
closure are required for a distributable package. A linked executable is not a
self-contained download. Review [release failures](NH_RELEASE_FAILURES.md) and the
whole workflow graph before CI changes.

Observe a dispatched CI build's terminal result for the intended revision;
dispatch or preflight success is not build acceptance. Diagnose failed runs and
retain failure evidence privately. Retry an unchanged revision only when evidence
identifies a transient infrastructure failure; otherwise correct the cause in a
new revision. Before announcing delivery, audit the actual downloadable assets
and their source/content identity, and verify the release has its intended
visibility. A successful build alone does not prove publication.

## Gates

1. Define player-visible scope, actual identifiers, provisional rules, AI and
   persistence requirements.
2. Validate production loading/schema admission, negative inputs, authoritative
   actions, old/new saved-rule contexts and detached predictions.
3. Compile affected targets; check portability at coherent integration boundaries,
   especially public headers, DLL linkage, filesystem and serialization.
4. Freeze exact identities and a bounded independent test route.
5. Run native and ordinary rendered checks relevant to that scope. A parser mock
   is not real loading; native tests are not visual acceptance. Preserve failed
   evidence rather than repeatedly testing unchanged failing bytes.
6. Independently verify package content, provenance, corresponding source, privacy
   and dependency closure. Test committed-pack distribution without overlays.
7. Deliver only after the applicable gates; state Linux/Windows interaction limits
   separately from build and checksum results.

Graphical tests use an isolated background display and disposable profile, never
host focus/input, and are silent. Launch through the maintained guarded helper,
verify both actual child SDL audio driver variables are `dummy`, and bound input,
captures, duration and cleanup. Do not bypass ownership/isolation gates.

## Local playable selection and recovery

The ordinary Linux launcher follows the latest successfully validated immutable
snapshot, not the newest compiled binary. Use
`tools/ci/linux_playable_snapshot.py validate` with a reviewed bounded driver
testing its exact environment-bound snapshot. Successful validation and atomic
promotion are one operation. A compile/checksum/trivial zero-exit driver is not
gameplay acceptance. Failure, timeout, changed or superseded bytes leave the
current selection unchanged. Explicit promotion is for reviewed rollback or
separately managed previews, not routine delivery.

Exact-PID graphical owners must not be wrapped by an incompatible process-group
supervisor. The [snapshot runbook](../tools/ci/LINUX_PLAYABLE_SNAPSHOT.md#signal-free-completion-for-externally-owned-acceptance)
documents the signal-free alternative: a separately reviewed endpoint retains
the original in-process validation ticket from before launch through actual
acceptance and full preservation checks, then completes the same atomic
selection transaction. Saved receipts cannot substitute for that endpoint or
authorize later manual promotion.

Before rollback or cleanup, verify the selected manifest, checksums, process and
worktree references, and preserved original inputs. Keep raw execution receipts
and local recovery paths private. The withdrawn 0.85 preview is not a current
Version 1.0 release candidate. Source implementation, verification and playable
delivery remain separate states.
