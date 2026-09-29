# New Horizons — release failures and regression lessons

## Purpose

User requested durable notes after successive Windows release failures. New agents
must read this with [NH_DELIVERY_PIPELINE.md](NH_DELIVERY_PIPELINE.md) before
changing packaging or dispatching CI. This is an incident/regression index, not a
substitute for current [NH_BUILD_HANDOFF.md](NH_BUILD_HANDOFF.md) and independent
[NH_WINDOWS_ACCEPTANCE.md](NH_WINDOWS_ACCEPTANCE.md) evidence.

Different errors appearing in successive runs are not necessarily recurrences of
the same bug. They show incomplete end-to-end coverage and serial discovery of
prerequisites. A successful fix proves only its tested scope. A regression test
listed below is a coverage location, not a claim that the latest CI passed it.

## Growth release follow-up

### September 29 local Holy Armor native-gate incidents

- The first Linux `vcmitest`/`vcmiclient` build over committed
  `c0fcec5d6` plus the uncommitted Holy Armor slice stopped in
  `AI/BattleAI/BattleEvaluator.cpp`: `std::clamp` received a `si16`
  duration with `int` bounds. Explicitly converting the duration to `int`
  repaired that production compile error.
- The incremental test build then stopped in
  `NewHorizonsMagicalDamageReductionTest.cpp`: a raw
  `SpellIDBase::Type` enum was passed to `BonusSubtypeID`, which requires a
  `SpellID` value. The explicit wrapper repaired the test fixture. Both Linux
  targets then linked.
- The first curated active-profile 25-case filter passed 24 and failed the
  positive Holy Armor AI choice. Instrumentation established that the
  player-specific battle callback rejected `canBeCast(CREATURE_ACTIVE)` for
  a visible opposing creature on the friendly turn, even though its public
  SPELLCASTER/CASTS bonuses identified a magical damaging spell. The threat
  forecast no longer asks whether that enemy can cast *now*; it uses visible
  remaining casts and spell traits. It also accepts damaging spells lacking
  the older `offensive` flag. A bounded health-value fallback makes protection
  of a temporarily nonattacking stack worth considering. Temporary diagnostics
  were removed. The final curated filter passed 25/25; 39 focused Python
  tests and the generated-module check pass. Windows compilation, visual
  rendering, Fire Shield interaction, and playable delivery are not claimed.

### September 29 local Empower Spell native-gate incident

- The first Linux `vcmitest`/`vcmiclient` build over committed head
  `603a68db3` plus the uncommitted Empower slice reached
  `lib/spells/ISpellMechanics.cpp` and reported a five-argument call resolving
  to the three-argument `Mechanics` member scaler. The build was stopped after
  independent review also found a potential signed-overflow product in the
  Regeneration rate and generic duration changes leaking into old snapshots.
- The repair uses the contextual three-argument member call, the checked
  rational scaler for Regeneration's `1500 × Spell Power` term, and a saved-v3
  Spellcraft-field gate for generic duration rescaling. A maximum-input
  arithmetic test and older-v3 duration assertion now cover the defects.
  The resumed Linux build linked both targets. A further adjacent filter
  exposed Spell Lock saturation arithmetic overflow under a maximum-int32
  Warcasting input; the repair caps the positive product before multiplication,
  preserving the existing duration cap. A narrow independent review found no
  blocker, and the final focused/adjacent spell filter passed 69/69 cases.
  These results do not prove
  Windows compilation, Mass Slow interaction, broad Lua parity, or playable
  rendering.

### September 28/29 local Basic Spellcraft native-gate incidents

- The first coordinated `vcmitest`/`vcmiclient` build stopped on a test-only
  `SecondarySkill::decode(string_view)` call in `FocusMagicSpellTest`; this API
  requires `std::string`. The incremental build linked after explicit conversion.
- The first active-mod 15-case filter failed only the new Cure fixture: 100
  damage to 100 Pikemen left no surviving wound, so Cure correctly rejected
  an attempted resurrection. The fixture now uses a wounded Archangel stack;
  the resumed 15/15 run passes.
- A broader 32-case saved-rules/Focus Magic filter found a stale synthetic v1
  fixture retaining New Horizons common spells (v1 forbids them), plus two
  Regeneration expectations copied with a 100000-millionth base rather than the
  actual 250000 base. Corrected fixture/expectations pass 32/32. The offline
  perk-data gate also lacked the previously activated Herbalist in its expected
  set; correcting that inventory restores its 64-case bundle. These were
  fixture/expectation errors, not a claim of playable delivery.

### September 28 local Nature Poison native-gate incidents

- Failure ID/stage: local `vcmitest -j12` over committed base `e6545354a` plus
  the uncommitted Nature Poison slice. The first compile stopped in
  `NewHorizonsCureTest.cpp` because a test called non-public
  `newHorizonsMagic::spellAllowedBySavedRoster`; a later incremental compile
  stopped in `NewHorizonsMagicAITest.cpp` because `SecondarySkill::decode`
  needs `std::string`, not the `string_view` constant. Both were test-only API
  errors. The owners switched to the public availability header and explicit
  string conversion; the incremental `vcmitest` target then linked.
- Focused execution initially discovered nine tests but skipped all nine
  because two existing TEST presets omitted `new-horizons`. A fresh isolated
  `testModSettings.json` with `core`, `vcmi`, `vcmi-test`, and `new-horizons`
  is required; ordinary `modSettings.json` is not enough for `vcmitest`.
  Never count a zero-failure run with all cases skipped.
- The first active-profile eight-case run passed five and failed three. Two
  recast fixtures expected Basic Nature's 77-damage base without giving the
  hero Basic Nature; the real unranked base is 70. The AI fixture enabled
  expanded hero ratings while disabling required hero-command rules. Repairs
  changed only fixture setup. The subsequent incremental link and active-
  profile focused run passed 8/8 with zero skips; saved-v2 roster exclusion
  and adjacent Magic Arrow AI cases each passed 1/1. No playable or Windows
  target result follows from this local native gate.

### September 28 local Phase 1 Blacksmith/Regeneration build

- Failure ID: local `cmake --build build/new-horizons-linux --target vcmitest vcmiclient -j12` over source head `82567abae` plus the uncommitted UP-024/Regeneration checkpoint.
- Observed stage/error: compilation stopped at `AI/Nullkiller2/AIGateway.cpp:91`; `const auto * stack = slot.second` could not deduce a pointer from `std::unique_ptr<CStackInstance>`. No linked target or runtime test result was produced by this attempt.
- Confirmed cause/fix: the new AI machine-utility scan iterated an army map whose values are owning pointers; use `slot.second.get()` to inspect the stack without transferring ownership.
- Guard/result: the exact AI translation unit compiled in the resumed build, and both Linux `vcmiclient` and `vcmitest` linked successfully. The focused AI source guard passes, but it did not catch this C++ type error; native compilation remains the necessary gate. The subsequent focused runtime filters are recorded separately from this build repair.

- Full34089398757 passed the real CRT gate, then MSVC rejected the level snapshot's
  unsigned-to-int brace conversion (C2397). GCC's permissive build had not made it
  fatal.35119d534 changes only that field to the actual hero level type, ui32.
  `test_windows_level_snapshot.py` extracts the real declaration and compiles it
  with MSVC or GCC -Werror=narrowing before the expensive client build. Local old
  reproduction fails; fixed declaration/native client and85 package cases pass.
  Corrected FULL34093695275 is monitored separately, not presumed successful.

- Full34087844032 atbc376 failed **before compilation** in the new CRT gate:
  `ModuleNotFoundError: pefile`. The82 Windows package tests and complete source
  preflight passed, but synthetic CRT tests mocked the parser and did not provision
  the real CI dependency.322cbe025 pins `pefile==2024.8.26` alongside Conan, runs a
  real import/version smoke before regressions, and adds an ordering control.
  83 offline tests pass; corrected FULL34089398757 is separately monitored. Do not
  report it successful until actual terminal evidence. The retained failure report
  and logs remain authoritative; no compilation/game archive occurred in340878.
- Actual a0 CRT versions14.29.30157.0 differed from the notice collector's available
  redist directory14.51.36231. Future provenance now separates the environment from
  byte-matched CMake-selected runtime sources and retained terms. An actual decoder
  and synthetic VS-tree tests do not reconstruct a historical runner's copy origin.
- Activating the exact GUI-tested growth metadata exposed two separate controls:
  the CMake canonical comparator omitted Heroes, and the older full-book export
  assumed legacy200mana for authored Knowledge20. Add Heroes to configure inputs/
  equality; preserve explicit legacy200/new20 expectations and manaLimit checks.
  Final11 data checks and36/128/39 native scopes pass; original REDs retained.
- A clean-prefix Linux build can still lose build RPATH during ordinary install.
  First staged ldd could not resolve adjacent libvcmi without launcher environment.
  Explicit CMake install `$ORIGIN` and build-with-install-RPATH required only normal
  relinks. Do not patch frozen ELF strings. The copied developer launcher also has
  a development-tree default; packaged entry-point behavior needs its own test.

## September 27 feature-build incidents

### September 28 local School-rank build check

The later September 28 full native build deliberately reconciled the curated
module against all current canonical config sources with
`tools/update-new-horizons-module.py`, then verified its `--check` output
before normal CMake regeneration. This supersedes the earlier dirty-tree
blocker for this checkout; it does **not** make that earlier partial link a
validated release. The Linux `vcmi`, `vcmiclient`, and `vcmitest` targets now
link. Several first-discovered test compilation errors were stale fixture API
references: typed `SpellID` wrappers are required for bonus IDs;
`getEffectDuration()` mocks return an integer, not `optional`; bonus collections
iterate `shared_ptr<Bonus>`; a creature Defense bonus is
`PRIMARY_SKILL`/`PrimarySkill::DEFENSE`; and battle AI fixture tests must
include complete callback/handler types. The corrected object builds are
recorded separately from runtime pass/fail.

Running `vcmitest` from the ordinary build root skipped every New Horizons
Bless/Bulwark case because the TEST preset did not activate `new-horizons`.
Native gameplay evidence requires a private profile with the curated module
and `vcmi-test` both mounted. The isolated v3 profile under
`build/nh-current-v3-native` exposes the actual failures; a green build or a
zero-failure test invocation with all relevant tests skipped is not success.

The first September 28 attempt to deliver the Linux garrison split UI used
the active dirty build tree with a filtered Ninja manifest. The client objects
compiled, but final linking failed on unresolved `newHorizonsArchery::*`
symbols: `lib/CMakeLists.txt` lists the newer `NewHorizonsArchery.cpp`, while
that build tree's stale `build.ninja` had no object rule for it. Normal CMake
regeneration was also blocked by the independently dirty creature-category
module mirror. Do not promote or treat that mixed build as playable. Delivery
used a clean detached checkout at `8c4ad7e5f` and a fresh CMake build instead;
that client linked and its frozen snapshot passed the bounded headless smoke.

The dirty-tree Linux `vcmi` production target linked successfully with the
version-3 School-rank and Transfigure Matter code using a Ninja manifest that
skipped only CMake regeneration. Normal regeneration was blocked by a stale
combined module settings mirror: its magic/perk sections match their canonical
sources, but its creature-category section does not match separate dirty work.
Do not run the broad module generator merely to clear this guard.

The full `vcmitest` link remains unverified. Compilation exposed several
pre-existing test fixture gaps (missing complete serialization types, an
unknown `STACKS_DEFENSE` bonus name, a const stack passed to a mutating
helper). The Demonic Gating, Unique Building Training, and Hero Command AI
test translation units were repaired and compiled individually; the next
unrelated full-target failure has not been pursued. These test-target repairs
are not evidence that the newly linked production library or a playable client
passed. To avoid delaying gameplay work on a chain of stale fixtures, use
focused data/source checks and the production-library build for the current
School-rank slice, while retaining the missing native execution/full-suite
validation as an explicit release gate.

These failures came from successive gameplay-feature commits rather than from the
Windows packaging route. They are retained because a later successful run does not
make the failed compiler evidence disposable.

| Failure / evidence | Cause and repair | Prevention / later target evidence |
|---|---|---|
| Run `36279452019`, head `db4905a5f`: MSVC reported that `spells::Mechanics::isMetamagicFollowup` did not exist while compiling the Lua registrar. | The Metamagic refactor left the Lua-facing interface without the virtual method used by its binding. `9cd3ac7e6` restored the interface contract. | `client/tests/check-new-horizons-metamagic-prompt.py` now checks the binding/interface seam. Run `36282269395` completed successfully with the repair. |
| Runs `36293431069` (head `1dd212f05`) and `36295633007` (head `e960efa1e`): MSVC rejected mutation of `masterGateUsed`, `pendingGateHexes` and `pendingDemonicGates` through a const battle object. | The Master Gate packet visitor obtained a const state while applying an authoritative mutation. `20ac211fe` uses the mutable battle-state path. Both runs exposed the same defect; they are not two independent diagnoses. | The repaired source passed this compile point and run `36296409606` completed successfully. Any packet visitor that mutates battle state must obtain a mutable state explicitly; `const_cast` is not the default repair. |
| Run `36298149952`, head `132b7ff9e`: MSVC reported that `pursuitMovementRemaining` was not a member of the abstract `battle::Unit` interface. | Pursuit read concrete mutable-stack state through the narrower unit interface. `b0b03b796` accesses the concrete state at the authoritative server seam. | Later full run `36311249815` compiled the repaired route successfully. New state fields must be read through the interface that actually owns them, or be deliberately promoted to a shared interface. |
| Run `36301307205`, head `b0b03b796`, and run `36303570540`, head `0d99e1580`: MSVC rejected two mixed-type `std::min` calls in Cleave hex selection, first in authoritative lib code and then in the duplicated AI projection. | `int` and `BattleHex`'s signed storage type made template deduction ambiguous. `676b221c5` fixed the authoritative expression and `fe144fecc` fixed the remaining AI expression. | Run `36311249815` completed successfully. When authoritative and AI calculations mirror one another, review both call sites together and use an explicit common type/helper instead of relying on platform-specific template conversion. |
| Run `36308724192`, head `71de5cd37`: MSVC rejected `BonusList::empty()` because `NewHorizonsOffense.h` only saw the forward declaration. | An inline helper dereferenced `BonusList` without including its complete definition. `b863215c5` added the owning header. | Runs `36311249815` and `36315781168` completed successfully. Inline header code that calls members requires the complete type; successful compilation in an unrelated translation unit is not sufficient evidence. |
| Run `36343003125`, head `b916c75ab`: MSVC rejected Spell Lock AI valuation in `SpellTargetsEvaluator.cpp` because `CSpell` was incomplete. | The translation unit called concrete `CSpell` methods while including only the public spell interface/forward declaration. The repair adds the owning `lib/spells/CSpell.h` header at the use site. | Exact-head Windows run `36353181774` succeeded, compiling BattleAI and publishing package artifact `10943952550`; this proves the compiler repair, not playable behavior. |
| Run `36347971057`, head `444250b5f`: after the `CSpell` include repair, MSVC reached the same Spell Lock AI translation unit and rejected `.value_or(1)` on `spells::IBattleCast::Value`. | `spells::Mechanics::getEffectDuration()` returns a plain `int32_t`; the implementation had confused it with the optional duration on a different cast interface. Commit `ddcd57391` removes `.value_or(1)` and clamps the integer result directly. | Exact-head Windows run `36353181774` succeeded, compiling BattleAI and publishing package artifact `10943952550`; playable Spell Lock behavior remains a separate gate. |
| Targeted Linux compile-database syntax check of committed transfer/UI head `3865be695`: `CExchangeController.cpp:154` reported `LIBRARY` undeclared. | The new exact-one gameplay explanation used `LIBRARY` without including its declaring `lib/GameLibrary.h`; another UI translation unit's include did not make this declaration available here. Add the direct owning header. | The repaired translation unit passed a repeated `-fsyntax-only` check; the other six transfer/UI units and three focused source guards passed. A succeeding exact-head Windows client build remains required. The queued `3865be695` job predates this repair and cannot prove it. |
| Run `36350655648`, head `ddcd57391`: MSVC reached `BattleActionProcessor.cpp` and rejected the Counterfire `AttackDescriptor` initializer because `.archeryRangedDamageMultiplierPercent` followed `.counter` and `.archeryCounterfire` (C7560). | C++20 designated initializers must follow member declaration order; the previous Linux syntax check accepted this ordering. The reviewed Archery commit `007510b61` places the damage multiplier before the counter fields. | Exact-head Windows run `36353181774` succeeded and published package artifact `10943952550`; no playable Counterfire acceptance is inferred. |

For every future terminal CI failure, append the run/head, exact failing stage,
confirmed cause, repair commit, regression guard (or an explicit statement that no
focused guard exists), and the first succeeding target-platform run. Do this before
the incident is considered closed; do not rely on chat history or a green successor
run as the only record.

## Recorded failures

| Failure / evidence | Cause or supported diagnosis | Guard and verification scope |
|---|---|---|
| `avformat-63.dll -> ncrypt.dll`, run 33991718723 | PE audit did not classify a legitimate Windows system DLL. | `tools/tests/test_windows_pe_audit.py`: narrowly classified system imports; unknown missing runtime DLLs still fail. Do not extend a blanket DLL allowlist. |
| Microsoft toolchain/SDK notice discovery | Notice lookup did not reliably supply full redistribution terms; a guessed URL redirected elsewhere. | Pin/review the official full document and hash; require installed SDK terms. `bf7dec157` records repair. A successful HTTP response is not proof of the correct document. |
| `opengl/system` lacked a conventional dependency license | Metadata-only Windows system provider was treated as a redistributable implementation. | `test_windows_system_dependency.py`: exact reference/platform and empty-payload checks; unexpected payload/missing real dependency licenses remain failures. |
| Old Windows artifact omitted dav1d, Brotli and PlutoVG source/notice coverage | Auditing shipped DLL names did not establish the full statically linked dependency inventory. | Full Conan graph plus binary identity, static nodes and exact source roots; `test_windows_notice_overlay.py` and independent old-payload repack audit. An accepted repair of old bytes is not a new mod build. |
| md4c missing `patches/0.5.2-0001-honor-vc-runtime.patch` in `source()`; run 34040567531 | Copied recipe lacked required exported source files. | `test_windows_dependency_notices.py`: manifest-safe paths/checksums, recover exact `md4c/0.5.2#3d7106721e458f9f799b87d4d50d02e0` exports in an isolated cache, retain patches in source archive. `98bd74f52`; later CI explicitly downloaded/applied the patch. Never skip the patch or upgrade the dependency to conceal missing exports. |
| StepSecurity subscription failure; run 34070234107 | CI action required a subscription for a private repository. | Check action plan/visibility compatibility before dispatch. `30db54ce6` replaced both subscription-gated providers with pinned upstream ilammy/msvc-dev-cmd and hendrikmuhs/ccache-action; run34070750674 passed those actions. User independently changed visibility back to public. Workers must not change visibility or disable security controls as an automatic workaround. |
| Setup 50 checks passed, then Managed-Preset smoke failed; run 34070750674 | `$PSScriptRoot` was empty when a parameter-default `Join-Path` expression was evaluated on Windows PowerShell 5.1. | `tools/windows/tests/Managed-Preset-Smoke.ps1`: resolve default in script body; preserve explicit override and all assertions. Default/explicit-path local PS7 checks passed; they alone do not prove PS5.1. Execute both smoke scripts with the real supported Windows shell. |
| SQLite `3.53.4#89fcf5cda598966acb7f3e185b19c58d` missing dependency license text | Conventional license-filename discovery missed the public-domain dedication embedded in upstream source. | Build has added header-notice extraction and tests in `test_windows_dependency_notices.py` for no standalone LICENSE, missing notice and incomplete notice. Verify exact-source header retention and verbatim complete notice; recipe MIT text is not a substitute. Current test/CI disposition belongs in Build handoff, not an assumed PASS here. |
| Local MinGW SDL_ttf unresolved `__imp_plutovg_*` | Static/shared declaration mismatch in the Windows dependency route. | Supported shared PlutoVG route repaired dependency build. Keep import/export configuration and graph identity in cross-build evidence. MSVC success never proved MinGW compatibility. |
| Local Windows nested link expressions, wide filesystem path constructor and `SDL_main` link failure | CMake/compiler/backend-specific compatibility defects encountered on the new MinGW route. | Separate fixes `b6f58bbae`, `f573a59bf`, `95a7001e3`; actual Windows build/install exit0. Preserve Windows incremental compile/link gates after public-header, SDL, filesystem and CMake changes. These failures are not evidence that the earlier MSVC executable was absent. |
| GNU runtime DLL omissions and Ogg DLL-name mismatch | Successful link used development-machine dependencies not yet closed in the deploy directory. | `test_mingw_runtime.py`, PE import closure on actual packaged files, exact DLL provenance. Do not ship an EXE alone or guess filename aliases without verifying the binary. |
| Six-school schema errors and unresponsive All tab after successful compilation | Runtime schema used unsupported validation features; hover state changed before toggle click dispatch. | Fix `034238454`; actual config-loading and normal-click GUI retest closed those observed defects. Add production-validator checks and real input routes early; compilation cannot establish either behavior. |
| Local034 ZIP contained173 home-path strings in nine PE data sections | Binary `__FILE__` strings and FFmpeg configure-tool paths escaped a text-file-only privacy scan. | `test_binary_privacy.py`: whole-binary ASCII/UTF16 scan, data-section and alignment controls; exact9/173 RED reproduced. No publication, debug-only stripping or binary string patching. Rebuild/remap and re-audit required. |
| Local remapped libiconv configure failed77 | Autotools split a space-bearing prefix flag; quote characters inside CFLAGS were not shell syntax. | Dependency remap must use space-free source roots, and real dependency compilation must verify it. A tiny compiler probe is not whole dependency acceptance. |

## Cross-platform fixture lesson — case-collision regression

A later Windows regression run passed 70 of 71 tests but errored in
`test_mingw_runtime.MinGWRuntimeTest.test_case_collision_rejected` with missing
`pefile`. The fixture tried to create `VCMI_lib.dll` and `VCMI_LIB.DLL` as separate
files. On case-insensitive Windows storage this overwrote one entry, so the
collision guard was never exercised and the test fell through into PE parsing.
Installing the parser alone would not repair the missing collision condition.

Build changed the test to provide an explicit case-colliding input listing on
either host and assert that `inspect_pe` is never called. Dispatcher independently
ran the then-current aggregate regression suite: 72 tests, exit0 (one additional
case had been added since the failing 71-case CI). Evidence is
`build/new-horizons-windows-cross/package-regressions-dispatcher-review.log/.json`.
This local result is not the subsequent Windows CI result; Build records that
separately. No production collision check was removed.

**Prevention:** filesystem-case, symlink, path-separator, locale and shell-version
assumptions in fixtures must be explicit. For a synthetic early-rejection test,
model the impossible-on-this-host input and assert later parsing/execution is not
reached. Keep real target-platform integration checks as separate evidence.

## Required prevention workflow

Build owns implementation of these checks; documentation does not mean automation
already exists. Use this list as the readiness review before the next dispatch:

- Run all relevant existing package/source/PE/setup regressions, not only the
  newly failing dependency's test. Record invocation, exit and source identity.
- Audit the entire resolved dependency graph, including static and header-only
  dependencies that need notices/source coverage. Preserve exact recipe/package
  revisions, options and source hashes for the frozen payload.
- Where failures are independent and safe to collect, inspect all dependencies and
  emit one structured preflight report rather than stop after the first missing
  notice/source. At the end, fail the gate if any entry failed. Never treat a
  partial report/archive as publishable. Abort unsafe extraction immediately;
  aggregation is not permission to continue executing untrusted broken inputs.
- Keep source/notice and shell smoke preflight ahead of the expensive client
  compile. Use exact Windows PowerShell 5.1 for its supported launcher/test path;
  PS7-on-Linux is supplementary evidence. Do not infer CMD/GUI success from it.
- Retain concise failure reports as CI artifacts even on failure. Reuse matching
  compiled artifacts for packaging-only corrections, with explicit base/source/
  packaging identities; never substitute an older feature payload unnoticed.
- Compare a changed graph against the last accepted graph before reuse. A new
  dependency version or compiler route invalidates the relevant prior evidence.
- Add positive and negative tests with each repair. Test that genuinely missing
  sources/licenses/DLLs still reject; do not solve a failure by weakening a gate.
- Read the actual final exit and independent acceptance report before pushing a
  ready claim or promising a release time. Build, package, launch, gameplay and
  publication are separate stages with separate evidence.

## How to append an incident

Add a row and a short checkpoint to the existing Build handoff containing:

```text
Failure ID / CI run or local command / frozen source identity:
Observed error and affected stage:
Confirmed cause (or explicitly unconfirmed hypothesis):
Minimal fix / regression test names:
Local result / actual target-platform result:
Remaining gate and next action:
```

No credentials, workstation paths, purchaser content or raw research dumps in
these notes. Keep historical failures even after repair, but label their scope.
Do not claim the pipeline is future-proof: tests reduce recurrence and catch more
failures earlier; new dependencies and environments can still reveal defects.
