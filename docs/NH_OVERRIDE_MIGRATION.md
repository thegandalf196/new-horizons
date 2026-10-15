# Legacy Overrides migration: bounded decision record

The Overrides transition was completed in September 2026 while the DOCX was
canonical. All 41 entries received incorporated, reconciled or superseded
dispositions. The [private game-design canon](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md) subsequently
became the sole gameplay authority; the DOCX remains historical comparison.
This record is retained to explain accepted replacements, not to reopen a
runtime override layer or repeat execution diaries. New amendments belong in the
private design library's `proposals/`. Public Pending Changes is a read-only
legacy migration input; its accepted decisions are preserved until incorporated.

## Substantive resolutions

- One ordered Basic, Advanced and Expert perk slot replaces ambiguous multiple
  choices per tier. Exceptional rank advancement does not bypass perk ordering.
- Class growth uses twenty deterministic points, starting = five times growth,
  plus the specified independent Skill attribute rolls; older ten/eighteen-point
  and no-roll proposals are superseded.
- Inscribed eligible combat spells are castable without School rank; ordinary
  acquisition remains gated. Normal/Buffer pools and event-driven capacity follow
  [Spell Point migration](NEW_HORIZONS_SPELL_POINTS.md).
- Arcane Acquisition replaces Countersequence under a distinct ID; historical
  saved snapshots remain distinct. Grand Metamagic is automatic on the specified
  consumed sequence, not a manual activation.
- Astral Nexus restores Normal capacity on each visit. House of Wisdom sells six
  distinct eligible scrolls at 1,000 Gold per spell level rather than teaching
  Skills. Arcane Reservoir grants weekly +50 Buffer without Normal refill.
- Mage/Arch Mage retain ordinary shooter melee penalties.
- Gating reservations cover the complete footprint and remain impassable; arrival
  uses flame-only marking and success-only sound. Infernal Beacon lasts through
  the first actionable round, including Swift Gate. Reinforced temporary HP
  creates/heals no creatures, and Mobile Gate validation retains reserves atomically.
- Hero Action presentation is contextual with generic Skill statuses, not three
  hardcoded counters. Distinct Orders coexist. Centered casting dialogs, current
  acquisition/help and dynamic Overcharge previews remain explicit requirements.
- Ice Bolt does not inherit the discarded Frost Bolt Speed-reduction clause.

## Resolved entry inventory

The following dispositions describe the migration checkpoint. Mentions of pending
runtime verification are historical, not current failures; consult source-linked
[content](developers/NewHorizonsContent.md), [integration gaps](NH_INTEGRATION_GAPS.md)
and [regression guards](NH_REGRESSION_GUARDS.md) for engineering acceptance.

| ID | Legacy entry | Current disposition |
| --- | --- | --- |
| L01 | Reserved Gating arrival area | Canonical design incorporated; runtime/playable verification remains separate |
| L02 | Replace Metamagic's countered-spell perk | Canonical/registry replacement integrated; Arcane Acquisition source implemented under a distinct ID, legacy Countersequence snapshots preserved; native verification pending |
| L03 | Core, Elite, and Champion recruitment layout | Canonical UI design incorporated: adaptive simultaneous bands, complete rosters, eight-stat icon cards, no dwelling previews |
| L04 | Astral Nexus function | User resolved in favor of automatic Normal refill; canonical and existing runtime aligned |
| L05 | House of Wisdom replaces Magic University | User resolved in favor of scrolls; canonical and existing runtime aligned |
| L06 | Solmyr Tower-versus-Inferno playtest profile | Canonical profile incorporated; runtime/playable verification remains separate |
| L07 | Exhaustive meaningful battle logging | Canonical causal and numerical logging standard incorporated; implementation coverage remains separate |
| L08 | Tower class and creature presentation | Canonical Battle Mage and coherent Mage/Genie dwelling/Library swap incorporated |
| L09 | Skill-development and action-help presentation | Canonical icon, probability-pane, help and Orders inspection/style requirements incorporated |
| L10 | Separate creature Speed and Initiative | Canonical separation and Mage values incorporated; old Frost Bolt clause explicitly superseded by current Ice Bolt/Vengeful Vines roster |
| L11 | Demonic Reserve and Gating interaction | Canonical design incorporated; engineering verification remains separate |
| L12 | First Demonic Gating perk tranche | Canonical design incorporated; final retirement verification complete |
| L13 | Second Demonic Gating perk tranche | Canonical design incorporated; Beacon uses first actionable round |
| L14 | Reinforced Gate temporary health | Canonical design incorporated with detailed temporary-HP semantics |
| L15 | Mobile Gate combined action | Canonical design incorporated with atomic combined-action guarantees |
| L16 | Magi melee penalty | User resolved and canonical DOCX integrated: ordinary melee penalties; runtime/playable verification remains separate |
| L17 | Art workflow for provisional assets | Workflow retained in design contract/asset register; canonical player-facing art coverage incorporated |
| L18 | Pending Gate marker | Canonical design incorporated: flame-only, no yellow outline |
| L19 | Metamagic grants a round-long Spell Action | Canonical now records consume-on-use, round expiry, no recursion and no Order use; source implemented, focused verification pending |
| L20 | Shared typed action system | General token currency superseded; canonical contextual typed allowances retain authoritative validation/AI/save consistency |
| L21 | Hero combat action panel | Three independent counts superseded; canonical contextual controls, generic Skill status and styled Hero Action state incorporated |
| L22 | Fort category headings and Conflux layout | Canonical equal-yellow headings and Conflux-specific correction incorporated |
| L23 | Preserve accepted inscribed-spell casting rule | Canonical design incorporated; runtime verification remains separate |
| L24 | Different Orders coexist | Canonical coexistence, independent state/expiry and boundaries incorporated |
| L25 | Unified spell access and Normal/Buffer Spell Points | Canonical design incorporated from linked specification; runtime/playable verification remains separate |
| L26 | Deterministic growth and secondary-skill offers | Old growth formula superseded; no forced Wisdom/School cadence, authored weights and Solmyr Wizard identity canonical |
| L27 | Perk ranks, missing choices, and empty slots | One-per-tier order and empty/help presentation canonical |
| L28 | All skill-teaching sources use current learning rules | Canonical consent, live-registry legality, advancement explanation and exceptional-rank boundary incorporated |
| L29 | Hero initialization and stale spells | Canonical cross-roster start/specialty/army audit and authored-replacement rule incorporated |
| L30 | Leadership displays | Canonical per-level growth, cost, total and current/max displays incorporated |
| L31 | Creature rank row and latest icon direction | Canonical horizontal row, stair-step rank icon and crown Leadership icon incorporated |
| L32 | Skills probability control and familiar panel styling | Canonical probability control, icon filtering and leather/red/gold panel styling incorporated |
| L33 | Spellbook inspection and casting dialogs | Canonical inspect-after-spending and compact centered modal incorporated; no automatic reopen/Wait workaround |
| L34 | Orders targeting, indication, animations, and art | Canonical battlefield targeting, indicators, non-spell state, gauntlet/style and distinct-animation direction incorporated |
| L35 | Meaningful logs and direct wording | Canonical direct causal/numerical log standard incorporated with L07 |
| L36 | Art coverage and status tracking | Player-facing coverage and Transfigure Matter binding canonical; workflow/status/provenance retained in design contract/register |
| L37 | Approved replacement primary-attribute table | Old growth model superseded; editable JSON and captured-save behavior retained as engineering acceptance requirements |
| L38 | Skill perk browser | Canonical left-click browser, rank groups, learned distinction and right-click help incorporated |
| L39 | Ordered Skill and perk progression | Resolved: ordinary alternation, one perk per tier, exceptional rank bypass without perk-order bypass |
| L40 | Passive Grand Metamagic | Older rejection explicitly superseded by newer canonical automatic Grand Metamagic; source/AI verification remains separate |
| L41 | Dynamic Overcharge outcome preview | Canonical refresh, current-state mitigation, casualty comparison and honest uncertainty incorporated |

Design migration does not imply native, rendered or playable certification.
