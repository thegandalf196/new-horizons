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
| Run `36343003125`, head `b916c75ab`: MSVC rejected Spell Lock AI valuation in `SpellTargetsEvaluator.cpp` because `CSpell` was incomplete. | The translation unit called concrete `CSpell` methods while including only the public spell interface/forward declaration. The repair adds the owning `lib/spells/CSpell.h` header at the use site. | `git diff --check` and the focused content checks cover source shape only. The following run exposed a separate duration-type error; the first succeeding Windows target run must still be recorded here. |
| Run `36347971057`, head `444250b5f`: after the `CSpell` include repair, MSVC reached the same Spell Lock AI translation unit and rejected `.value_or(1)` on `spells::IBattleCast::Value`. | `spells::Mechanics::getEffectDuration()` returns a plain `int32_t`; the implementation had confused it with the optional duration on a different cast interface. Remove `.value_or(1)` and clamp the integer result directly. | Verify the exact interface return type at the call site and compile BattleAI in the next Windows target run. The failed run uploaded only precompile/preflight evidence, no playable package. |
| Targeted Linux compile-database syntax check of committed transfer/UI head `3865be695`: `CExchangeController.cpp:154` reported `LIBRARY` undeclared. | The new exact-one gameplay explanation used `LIBRARY` without including its declaring `lib/GameLibrary.h`; another UI translation unit's include did not make this declaration available here. Add the direct owning header. | The repaired translation unit passed a repeated `-fsyntax-only` check; the other six transfer/UI units and three focused source guards passed. A succeeding exact-head Windows client build remains required. The queued `3865be695` job predates this repair and cannot prove it. |

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
