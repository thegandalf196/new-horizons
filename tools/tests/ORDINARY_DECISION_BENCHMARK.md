# Ordinary native decision benchmark

This opt-in workload captures one Full-policy BattleAI submission without applying
it authoritatively. It is not a whole encounter, rendered test or playable release
gate. Configure with `ENABLE_NH_NATIVE_BENCHMARKS=ON`, then explicitly build
`nhNativeAcceptanceBenchmarks`. It contains
`NativeRosterDecisionProfileTest.FourteenStackOrdinaryFirstDecision` and the
other opt-in native workloads. No benchmark command builds code or runs during
CMake discovery.

The fourteen-stack fixture uses seed1337, Normal difficulty/horizon2, grass,
battlefield0, no obstacles/tactics, no heroes, attacker slot1 Marksman with native
double shots, and defenders already Waiting. Its complete effective-input and
native-state readbacks accompany actions and both RNG streams. All candidate
arms must use identical resources and policies. Source presence is not execution.

Invoke through a verified absolute Python interpreter:

```sh
"${NH_PYTHON:?Set a verified absolute interpreter}" tools/tests/nh_ordinary_decision_benchmark.py \
  --manifest /private/reviewed-benchmark.json --manifest-sha256 REVIEWED_SHA256 \
  --output /private/fresh-results --execute
```

The reviewed JSON manifest has this shape; replace every example path/hash with
actual reviewed values. Each candidate's `pins` must exhaustively bind its frozen
source supplement, executable, facade/runtime providers and resource inventory.
Neither a Git commit alone nor an executable checksum establishes that binding.

```json
{
  "pairs": 3,
  "quietLease": {"path": "/private/quiet-lease.json", "sha256": "SHA256"},
  "candidates": {
    "baseline": {
      "binary": "/candidate-a/vcmitest",
      "cwd": "/candidate-a/resources",
      "environment": {"LD_LIBRARY_PATH": "/candidate-a/runtime", "XDG_DATA_DIRS": "/private/empty-data-search"},
      "pins": {"/candidate-a/vcmitest": "SHA256", "/candidate-a/resources/config/settings.json": "SHA256"}
    },
    "candidate": {
      "binary": "/candidate-b/vcmitest",
      "cwd": "/candidate-b/resources",
      "environment": {"LD_LIBRARY_PATH": "/candidate-b/runtime", "XDG_DATA_DIRS": "/private/empty-data-search"},
      "pins": {"/candidate-b/vcmitest": "SHA256", "/candidate-b/resources/config/settings.json": "SHA256"}
    }
  }
}
```

For `--gate`, each candidate also requires `expectedDiscoveryCases`: the complete
source-reviewed list of literal `Suite.Case` names compiled into its binary,
including the requested ordinary case. Keep parameter suffixes and `DISABLED_`
prefixes. Derive this inventory from the reviewed source and build configuration;
the listing being checked cannot establish its own expected membership.

Discovery is unfiltered and must match the supplied inventory exactly, without
missing, extra or duplicate cases. The body still runs only the requested case.
Outside `--gate`, an omitted inventory is explicitly reported as unverified;
supplying an inventory always enforces it. A successful XML result must contain
exactly the requested case with `status="run"` and `result="completed"`, no
failure, error or skip, and unique property names. These checks establish a
matched metric fixture; `candidateGameplayAccepted` remains false.

The coordinator-issued lease contains `state: "QUIET_EXCLUSIVE"`, an unexpired
`expiresUnix` timestamp, `buildsAllowed: false` and
`otherMeasurementsAllowed: false`. Its hash binds it to the manifest. It records
coordinator exclusivity; it cannot independently prove that unrelated processes
are idle. Do not run any compilation or competing measurement during the lease.

The runner creates HOME/XDG/TMP directories under each fresh output, sets both SDL
audio and video driver variables to `dummy`, excludes host display and preload
environment, observes the exact owned executable/environment, caps address space
at8GiB and each child-written file at64MiB, and waits/reaps each owned child.
Discovery and workload are separately bounded to120s plus10s shutdown grace.
Unobserved, failed, timed-out, changed or empty runs reject acceptance and retain
logs/owner receipts. Never retry a failure silently or use host audio/display.

Candidate working directories must preserve `test/testdata/` and approved read-only
purchaser Data/Mp3 mounts as described in the Linux build guide. Original assets,
maps and saves must remain external. This fixture is not an asset-free public CI
job. An unavailable original-data lane is unsupported/not-run, never a passing
skip. A self-owned synthetic replacement needs its own coverage statement.

The runner requires exact state/action/RNG agreement before writing a successful
summary, alternates A/B order and retains every individual result. It reports
median/min/max callback microseconds; no timing threshold or general speed claim
is inferred. Across serialization format changes, diagnose differing native-byte
witnesses and provide a separately reviewed semantic comparator rather than
silently discarding them. Policies that intentionally change AI choices need
separate decision-quality evidence and cannot pass this exact-choice harness.

For a separate diagnostic pass, add `--diagnostics`. This enables bounded existing
BattleAI counters under DEBUG; it changes observer overhead. Ordinary quiet passes
leave optional diagnostic logging off; existing shipping callback clocks remain. Work-counter availability is explicit; unavailable is not
zero. This hero-free workload has zero Hero Action submissions and establishes no
hero-option evaluation, rule compilation, map-load or persistence-byte coverage.
Keep those scenarios and instrumentation passes separate. Raw receipts, private
paths, profile contents and hardware-specific measurements stay outside Git.

The explicit relative gate is `--gate --max-regression-percent 10` with three to
five matched pairs. It checks exact parity first and exits nonzero on a candidate
median above the configured margin. The margin is provisional, hardware-dependent
and reported with individual samples/ranges. See the
[additional workload and CI contract](NATIVE_ACCEPTANCE_WORKLOADS.md) for the
opt-in resource-mounted entrypoint and coverage limits.
