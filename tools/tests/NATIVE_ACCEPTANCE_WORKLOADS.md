# Opt-in native acceptance and regression workloads

These generated fixtures supplement the [ordinary callback benchmark](ORDINARY_DECISION_BENCHMARK.md).
They use current shipped configuration and external purchaser resources. They
ship no original maps, saves or archives. All encounter/world cases are disabled
by default and require `--gtest_also_run_disabled_tests`.

| Selector suffix | Authored scenario | Measurement boundary |
| --- | --- | --- |
| `NoHeroFourteenStack` | Original ordinary fourteen-stack rosters, seed 1337, natural initial queue, no heroes | Authoritative encounter to result receipt |
| `HeroOptionsFourteenStack` | Same rosters with Orrin and Valeska, each with an authored Basic perk. Cure and Ice Bolt, 24 Mana, Spell Power 17 and Knowledge 24 | Same boundary with eligible Orders and spells |
| `CastleEncounter` | Christian, 14 Pikemen/9 Archers vs map-created 29 Halflings | Same boundary |
| `NecropolisEncounter` | Straker, 24 Skeletons/12 Walking Dead vs map-created 29 Halflings | Same boundary |
| `RampartEncounter` | Mephala, 18 Centaurs/9 Wood Elves vs map-created 29 Halflings | Same boundary |
| `ConfluxAenainGnolls` | Aenain with 14 Wisps, 2 Air Elementals and 2 Water Elementals against 24 map-created Gnolls. Native primary stats, Mana, spells and one authored Basic perk | Same boundary for an early-fight scenario |
| `SorceryEncounter` | Neela with 24 Gremlins, 12 Stone Gargoyles, external Expert Sorcery and one authored Basic perk. Focus Magic, Forced Recall, Force Lance, Cure and Ice Bolt. 80 Normal Mana, 80 Knowledge and zero Buffer | Same boundary with these spells available for selection |
| `MirrorCastle` / `MirrorRampart` / `MirrorNecropolis` | Identical seven-stack faction rosters without heroes. Quality trials vary the recorded seed. | Same encounter boundary with both seats evaluated |
| `MirrorCastleHeroOptions` / `MirrorRampartHeroOptions` / `MirrorNecropolisHeroOptions` | Mirrored creature rosters with distinct same-faction heroes on each side. Each hero uses native starting skills, perk, book and spells. | Same boundary with current Orders and hero spell eligibility. Quality comparisons keep the hero pair fixed and evaluate each AI variant from both seats. |
| `SmallMapInitialization` | Owned sparse 36×36 one-level map, two heroes/towns and one neutral | Generated H3M construction/cache dump, parse and world initialization |
| `LargeMapInitialization` | Same authored objects on a 108×108 one-level map | Same boundary; tile scale, not a dense realistic large scenario |
| `DayEightCurrentFormatMemoryRoundTrip` | Small map advanced through authoritative daily boundaries to day 8; current-format world snapshot plus explicit authority RNG envelope; day 9 control/reload continuation | Memory snapshot write/read; excludes disk, lobby and replay-history retention |

The encounter suite is `DISABLED_NativeEncounterBenchmarkTest`; the world suite
is `DISABLED_NativeWorldBenchmarkTest`. Encounters default to explicit Full policy; `--policy Fast` selects current
`BeamOneTwoActors`. They use fixed explicit positions, no random obstacle placement and
default seed 1337. `NH_BENCHMARK_SEED` in each candidate's pinned environment
selects a nonnegative integer seed through `INT_MAX` for a separate quality run.
The prototype's `NH_EXP_SEED` alias remains accepted when it agrees with the
selected seed. The recorded seed initializes the world, authority and AI
generators. Matched arms use the same seed. These encounters differ from the Full-policy one-callback fixture and from earlier
historical faction measurements. Faction/Sorcery defenders are actual `CGCreature`
objects; current neutral-size and creature-line rules apply during initialization,
and effective armies are recorded rather than normalized back to the authored
map counts. Whole encounters do not force the first-decision fixture's Waiting
state. A benchmark family comparison always keeps its own complete fixture fixed.

The maintained runner writes an explicit `core`, `vcmi`, `vcmi-test`,
`new-horizons` test preset in each fresh encounter profile before discovery and
execution. It does not change the world or ordinary benchmark presets. Encounter
fixtures reject missing or additional active mods. They verify two Basic
Skills, the exact authored Basic perk and a spellbook before any explicit
fixture spell or rank grants. The curated preset must
be applied to both source-matched benchmark arms. Old fixture binaries asserting
zero perks require the same fixture repair before comparison.

AI submissions are captured as values, then sent through
`BattleProcessor::makePlayerBattleAction` after the callback returns. Every
command must be accepted. A 512-callback/64-round bound rejects incomplete fights.
The result window query stays open; timing excludes acknowledgment, progression
and adventure continuation. Encounter wall time includes packet observers and
per-callback RNG-purity witnesses; summed callback time is a separate metric.
Outputs retain initial unit/hero/army/rule evidence, ordered actions, terminal
unit states (including exact available HP and a summoned flag for each terminal
unit), rounds, accepted packets, casualties, experience and both authority
and AI RNGs. Shipping exchange/probe work counters are recorded separately.
No expected winner or speed is invented before execution; the qualified baseline
provides the deterministic regression witness.

Run one workload with the same reviewed manifest/lease contract as the ordinary
runner, using a verified absolute interpreter:

```sh
"${NH_PYTHON:?Set a verified absolute interpreter}" tools/tests/nh_native_acceptance_workloads.py \
  --manifest /private/reviewed.json --manifest-sha256 REVIEWED_SHA256 \
  --output /private/fresh-results --workload castle --execute
```

Each candidate's `expectedDiscoveryCases` supplies its complete source-reviewed
binary inventory, including the requested workload. `--gate` requires this field.
Discovery lists the entire binary without a filter and rejects missing, extra
or duplicate cases. Preserve literal disabled-suite/case names and parameter
suffixes; the body retains `--gtest_also_run_disabled_tests`. A non-gate run
without an inventory reports unverified membership. XML must show exactly the
requested case as run and completed, without failures, errors, skips or duplicate
properties. Neither verified membership nor a timing pass accepts complete
candidate gameplay; that summary field remains false.

Parity excludes only exact known work-property names. Unknown fields remain
semantic even when their names end in `Exchanges` or `TargetProbes`. When the
versioned diagnostic schema is present, support, completion, policy and overflow
controls must pass validation before exclusion; samples retain them separately
as `diagnosticControls`. The original schema may omit the later three native-read
work counters, but a partially emitted extension is rejected.

`--diagnostics` enables optional DEBUG records and initialization profiling.
This requires an `ENABLE_NH_PERF_EXPERIMENTS=ON` build for world profiling. The
runner reads the actual `VCMI_Test_log.txt` under the disposable profile, rejects
missing/invalid/overflow initialization groups and missing HeroCapture evidence,
and retains per-type Hero Action rows and aggregates. A missing hero type row is
reported explicitly; it does not establish zero evaluations. `--volume` requires
a separate diagnostic invocation and enables the more expensive JSON-volume
inspection. Those values are not total object memory or decomposed serialized
owner bytes; rule compilation counts still need their own admitted instrumentation.

For a relative regression gate, add `--gate --max-regression-percent 10` and set
`pairs` to 3–5 in the reviewed manifest. Exact state/action/RNG agreement is required
first. The gate exits nonzero if the candidate median exceeds the matched baseline
median by the explicit margin. Ten percent is a provisional configurable noise
margin, not a performance target or proof of statistical significance. Individual
samples and min/max remain available; investigate noisy or thermal/contention
runs before trusting a pass. A5 search-policy changes that alter choices fail
this exact-choice gate. For a separate observational comparison, select
`--policy Fast --compare-choices` without `--gate`: it retains every differing
state/action/RNG witness and reports actual winner, casualties, XP, rounds and
ordered actions per arm. Initial fixture/seed/policy mismatches still fail. This
report neither passes exact parity nor accepts decision quality; representative
quality tests and a reviewed policy decision remain necessary.

The portable CI entrypoint runs only explicitly selected, nonempty workloads:

```sh
"${NH_PYTHON:?Set a verified absolute interpreter}" tools/tests/nh_native_benchmark_ci.py \
  --manifest /private/reviewed.json --manifest-sha256 REVIEWED_SHA256 \
  --output /private/fresh-ci-results --workload ordinary --workload castle \
  --max-regression-percent 10 --execute
```

The CI manifest additionally requires `harnessPins`: absolute paths and SHA-256
values for the entrypoint, both runners, `nh_native_work_properties.py` and the
actual interpreter, together with
each candidate's complete `expectedDiscoveryCases`. Candidate
inventories must cover source/test supplements, facade/providers, current config,
`test/testdata/`, and read-only external Data/Mp3 mounts. Missing reviewed inputs
produce `NOT_RUN` with a nonzero exit; a native/parity/regression failure is
`FAILED`, never a successful skip. No public asset-free integration runner or
self-hosted CI machine is supplied by this change. The pure accumulator tests are
separate coverage. This script is an opt-in entrypoint, not an always-green workflow.

Source-only or compile success does not prove these fixtures work. Compile both
candidates using the identical reviewed test supplement, then qualify actual
native execution before timing. Keep compilation and other measurements outside
the exclusive quiet lease. An existing optional RelWithDebInfo configuration can
support a separate symbolized profile; explicitly retain frame pointers (for
example `-fno-omit-frame-pointer`) and verify actual compile arguments. Profiling
and diagnostics remain separate from quiet timing and rendered/playable acceptance.
