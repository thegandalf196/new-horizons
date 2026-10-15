# Bounded performance findings

These are historical, scoped observations, not a certificate for current code, Windows hardware, GPU presentation or completed games. Raw runs and failed attempts are retained privately. Use the [measurement contract](NH_PERFORMANCE_EXPERIMENT.md) for new comparisons.

| Finding | Evidence and interpretation |
| --- | --- |
| Transport burst harness, source fd5abac02 | 960 records; four routes, four payloads, three bursts, ten repetitions, trace OFF/ON. Threaded bytes won the large 64KiB/64 burst versus same-thread queued bytes (271.475 vs 517.995 microseconds per completed command). Tiny typed work removes serialization and is not whole-game validation. |
| Formation-only comparison, source 187604d2d | Five alternating independent pairs, two real toggles each; all ten actual launcher exits zero and traces loss-free. Mean caller-scope saving 8.833 microseconds; submit→state/ACK were not consistently improved. No substantial player-visible benefit. Architecture experiment closed without changing defaults. |
| Selected-perk lookup | Avoiding repeated whole-registry validation in hot selected-perk queries improved the bounded synthetic lookup. Initialization, selection and save-load validation remain required. This did not explain every reported long turn. |
| Lazy attack-probe snapshot, 2026-10-11 | 31 focused native cases covered exact scores/state/snapshot counts. A matched 41-action/three-round replay took Tan 9723/9760ms: no overall speedup established. Separate supplied-save observations were not paired Windows comparisons. |
| Target-selection timing, 2026-10-11 | Native partition/parity checks passed. One Linux battle recorded combined movement search 5.328s and best-target selection 3.993s; callbacks were wall-time sums. A missing per-event runtime proof withheld full graphical acceptance despite closed counters. |
| Completed activation forecast | Defend, movement and activation-consuming creature casts include detached Plague completion; Wait/free casts do not. Nineteen principal and fourteen adjacent cases passed at that checkpoint. Correctness repair, not a measured speedup. |
| SDL3 outline kernel | Hosted Windows synthetic sparse 450×400 pixel-equivalence comparison: reference 178.9–180.8ms versus bounded kernel 1.21–1.24ms, reference first, uncontrolled OS cache. Component CPU result, not in-game FPS or general Windows gain. |

Inactive Veteran Cohesion construction and later probe/lifecycle partitions have focused correctness evidence, not automatic turn-time benefit. Rejected hero-classification hints and other private experiments must not be advertised as retained optimization without a demonstrated matched gain.

Player verification confirmed Cabir/Wisp playback repaired in Windows source `5229c808b15d` (outline/phase `d042cb3f4fd2`, movement `5a85daca0ad5`). This is distinct from AI responsiveness or measured frame-time evidence.

Slow Normal-difficulty AI turns remain a user-priority requirement. Preserve exact workload and full state/action/RNG checks, record measured scope and failed trials, and compare the actual relevant scenario before claiming improvement.
