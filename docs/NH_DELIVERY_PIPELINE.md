# New Horizons — development and delivery pipeline

## Authority

User-approved workflow: four implementation/testing workers are sufficient for
now. Improve integration and release scheduling before adding feature authors.
Read [NEW_HORIZONS_DESIGN.md](NEW_HORIZONS_DESIGN.md) for full product scope,
[NH_WORKER_PLAN.md](NH_WORKER_PLAN.md) for ownership and acceptance, and
[NH_AGENT_START.md](NH_AGENT_START.md) to start or resume role goals.
This document changes scheduling, not product requirements or evidence standards.

## Two lanes, one integrator

### Feature lane

Runtime and Frontend implement the full design incrementally, coordinating typed
interfaces, content identifiers, save versions and semantics before integration.
Build owns CMake/configuration integration; Tester supplies independent acceptance.
Future work may continue outside a frozen candidate. It must not modify that
candidate's files, installed resources or test profile.

### Release-candidate lane

Build freezes a small, coherent playable increment instead of waiting for the
entire redesign. Freeze includes source revision, binary hashes, configuration,
curated module/artwork, launcher, platform/build command and known limitations.
Uncommitted candidate-only corrections require a recorded diff identity; a final
release needs corresponding source matching the actual shipped payload.

Once frozen, only defects required for that declared increment enter its release
scope. Growth/mastery work must not delay a command/school preview merely because
it is newer. Conversely, do not ignore a broken control or save path already in
the candidate by calling it future work. Cut a new candidate identity after a fix;
never silently replace bytes that Tester is examining.

Use immutable copied candidates under the existing ignored build root, not a new
Git worktree/toolchain for every iteration. Retain concise manifests, logs and
representative screenshots; reuse toolchains and prune obsolete disposable output
only after checking no active worker owns it. Never touch purchaser assets.

## Per-increment gates

1. **Define:** concrete player-visible behavior, declared provisional rules, AI,
   persistence, identifiers and owned files. Keep unresolved design choices visible.
2. **Implement and test early:** actual production config loading and named-schema
   validation, negative inputs, old/new save contexts, authoritative human/AI
   commands and corresponding UI. A parser mock alone does not test the real loader.
3. **Cross-platform build:** compile affected Windows and Linux targets at bounded
   integration checkpoints, especially public-header, SDL, DLL/linkage, filesystem
   and serialization changes. Keep Windows from becoming an end-of-project surprise.
4. **Freeze:** Build supplies immutable source/binary/content identity and a precise
   test route. It coordinates the shared compiler and Tester quiet-run interval.
5. **Independent test:** Content verifies identity, runs bounded guarded journeys,
   and reports actual passes and defects. Include normal clicks, navigation, return
   paths, availability and effect feedback—not just dialog construction. Required
   gameplay defects go straight to the source owner, then fix/rebuild/new-identity/
   retest. Do not rerun an unchanged failing candidate in a loop.
6. **Package:** exact runtime DLL closure, engine resources, curated content,
   setup/launch helper, licenses and matching corresponding sources. Test package
   contents independently. A linked EXE is not a self-contained download.
7. **Deliver:** Build privacy-scans and commits/pushes coherent verified changes,
   then uploads the checked incremental preview to the private GitHub repository.
   Confirm release asset presence and identity before reporting a download link.
   Do not conflate a draft, source commit, old preview or Actions run with a newly
   published feature package. Native Windows gameplay limits must be explicit.

Focused source commits may precede full GUI acceptance if compile/test evidence and
remaining limits are stated. They are not a release/playability acceptance claim.
Do not weaken the package or first-increment gates merely to produce a link.

## Windows scheduling and fallback

Build/Integrator currently owns Windows compilation and packaging, as well as
Linux integration. Local assembly/test first remains preferred. The historical
MSVC Windows Actions build and the local Linux-to-Windows MinGW build are distinct
routes; success in one does not establish compatibility in the other.

After a bounded reproducible local toolchain failure, identify the next repair and
its evidence. Do not let repeated local-toolchain experiments indefinitely block
a user-facing preview. Build may use the previously working Windows Actions route
for the frozen source when appropriate, while preserving local repair work. Use
cheap source/notice preflight or repack for packaging-only changes; avoid repeated
full cloud compiles for those failures. No parallel compiler/dependency workloads
during reserved GUI/timing runs. Do not claim Wine is native Windows acceptance.

Git uses the configured github-gandalf SSH alias. GitHub API/release operations
need separate `gh` authentication; verify it when needed rather than assuming an
old authentication failure persists. Never expose tokens or change account identity.

## Optional fifth role: Windows Packaging

Not automatically created or activated by this document. Dispatcher may assign it
explicitly if Build's packaging backlog is demonstrably delaying delivery.
Before activation, record an exact file allowlist, frozen payload/source identity,
outputs and transfer acknowledgment in NH_BUILD_HANDOFF.md. Until that handoff,
Build retains all packaging ownership; a fifth session has no implicit edit rights.

The role owns only delegated Windows package/dependency/notice scripts and checks.
It requests source/CMake fixes from their owners, does not stage/commit/push, and
does not launch GUI tests. Build remains sole integrator/compiler scheduler and
publisher; Content remains independent package/GUI acceptance. No extra feature
author, orchestrator layer or new shared build root is implied.

## Dispatcher and continuity

At material transitions, inspect actual worker state, build exits, independent
results and remote publication. Promptly surface a real stalled lane or ownership
conflict and assign the smallest next action. Keep the user informed of completed
behavior, current failure, next gate and whether a download exists. Do not replace
work with repeated ACKs, decorative counters or another watchdog framework.

Workers retain one current checkpoint in their existing handoffs: changed source,
command/exit, evidence, unresolved defects, next action and arranged wake owner if
waiting. Plans are durable; tmux messages are wake signals. Formal goals follow
session-injected contracts and existing safety limits. A waiting goal needs an
arranged wake/deadline under the harness rules; no busy polling or invented blocker.
Do not promise unsolicited chat replies or execution while the machine is asleep.
