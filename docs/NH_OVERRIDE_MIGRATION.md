# Legacy Overrides migration audit

## Authority and retirement gate

New Horizons.docx is the sole canonical design specification. Pending Changes
holds temporary amendments only. This register preserves legacy decisions while
they are compared item by item; it does not grant them automatic precedence.

For each entry, verify every intended design detail, including referenced
specifications. Classify as incorporated, missing, conflicting, or explicitly
superseded/redundant. Integrate missing decisions into the relevant DOCX section
before discarding them. Conflicts require review, not automatic merging. Retire
Overrides only when all entries have a resolved disposition and verification.
Implementation evidence is distinct from design migration evidence.

## Current comparison baseline

Current repository DOCX SHA-256:
`7fd38c3b12386e62f511d66bfbd1ea615f301d1e2bdf57a9c70b831ff3ff81e4`.
The transition edits were integrated directly into the retained canonical file.
ZIP integrity passed, LibreOffice rendered all 191 pages, and affected pages 4,
5, 6, 11, 46, 97, 99, 100, 122, 161, 172, 174, 182, 183, 186, 188, and 189
were visually inspected without clipping or overlap.

## Perk registry audit — 2026-09-26

The 31-skill, 310-perk registry and its module copy now record the current DOCX
hash above. The focused audit compared all 31 authored perk tables. Registry
names, rank requirements, and descriptions were aligned to the latest canonical
rows where the detailed text is direct and unambiguous. This includes the
deterministic target/selection rules, automatic triggers, revised perk effects,
and the replacement of Countersequence with Arcane Acquisition. Arcane
Acquisition is now active in the registry and has a source implementation that
captures Focus Magic cast provenance while leaving legacy Countersequence save
state and perk snapshots intact. Focused native verification remains pending.

Intelligence now uses the approved 130% Normal/Buffer Spell Point model in the
DOCX and registry. Formula Reserve, Spell Buffer, Arcane Acquisition,
Spellbinder and automatic Grand Metamagic are also integrated into the canonical
tables and aligned with the registry. Runtime/save verification remains distinct
from this completed design reconciliation.

## Reviewed conflicts and dispositions

| Legacy decision | Current canonical text / section | Conflict |
| --- | --- | --- |
| Perk ranks; Ordered Skill and perk progression | Skill and perk development: exactly one Basic, one Advanced, and one Expert perk slot; exceptional advancement may bypass a Skill-rank step but missing perk tiers remain ordered | Resolved 2026-09-26: user chose the clearer existing runtime. Multiple perks from one tier are forbidden; Basic, Advanced, and Expert perk slots are filled in order. |
| Astral Nexus function | Unique buildings / Dungeon: every visit automatically restores Normal Spell Points to current maximum, with no Buffer grant or visit limit | Resolved 2026-09-26 in favor of the legacy refill rule and existing runtime. The discarded +5 permanent Knowledge rule is no longer canonical. |
| Replace countered-spell perk | Tower / Metamagic: the Basic pool contains Arcane Acquisition; each currently unmarked living enemy target receives two marks from eligible Focus Magic | Canonical and registry replacement is complete. New source preserves Countersequence only for historical saved snapshots and implements Arcane Acquisition under its distinct ID; native verification remains pending. |
| Unified Normal/Buffer model / Intelligence | Primary Attributes and Spell Points: Normal capacity is effective Knowledge; Intelligence sets it to floor(130% of effective Knowledge); Buffer is separate and spent first | Resolved and integrated from the linked Spell Points specification. Capacity correction is event-driven and Buffer sources never refill missing Normal points unless explicitly stated. |
| Inscribed spells and Spellbinder's Hat | Artifact conversion: every legitimately inscribed combat spell is castable without School proficiency; the Hat temporarily inscribes eligible Level 5 combat spells | Resolved and integrated from the linked Spell Points specification. |
| Passive Grand Metamagic | Tower / Metamagic: the first extra spell of the third used sequence automatically grants one further Spell Action | Canonical text now matches the authoritative source transition and AI projection; focused native verification remains pending. |
| Hero combat action panel | UI / Hero Action state and Faction Skill states: contextual Hero/Spell/Order availability plus generic provider-driven Skill statuses | Reconciled: the action panel remains, while Metamagic allowance is supplied through the generic Skill-status presentation rather than a hardcoded field. Visual verification remains separate. |
| Spellbook casting dialogs | UI / Magic Arrow Overcharge: compact centered leather/red/gold modal with live Mana, damage, and casualty comparison | Resolved 2026-09-26: user chose centered presentation. |
| Approved primary-attribute table | Primary Attributes / Starting Attributes and Growth: starting = 5 × growth; 20 deterministic points per level; Skill bonus rolls | Resolved by user: keep the new DOCX model. Legacy 18-point growth and no-bonus-roll rules are superseded. |
| Magi melee penalty | Tower creature rebalance / Mage and Arch Mage retain ordinary shooter melee penalties | Resolved by user and integrated into the canonical DOCX; runtime source test exists, playable verification remains separate. |
| House of Wisdom | Unique buildings / Conflux and Experimental Values: persistent stock of six distinct eligible combat-spell scrolls, priced at 1,000 Gold per spell level | Resolved 2026-09-26 in favor of the legacy scroll storefront and existing runtime. It teaches no Skills. |
| Arcane Reservoir | Unique buildings / Tower: once per week grants one visiting hero +50 Buffer Spell Points without restoring Normal points | Resolved and integrated from L25; the old multiplication rule is superseded. |

## Clause-level checks completed in this pass

### Gating entries L01, L11–L15 and L18

Canonical location: Inferno — Demonic Gating and its immediately following perk
pool. Related UI location: Faction Skill states. No entry in this group is yet
discardable solely on a shared perk name.

- **L01/L18 — Incorporated:** The canonical text now specifies an impassable
  reserved full footprint, double-wide coverage, a flame-only marker with no
  yellow outline, and the Devil movement sound only on successful arrival.
- **L11 — Incorporated design:** Owned reserve troops, whole-stack hero-screen
  deposit/withdrawal, Creature Activation cost, arrival timing, rank access,
  survivor return, permanent casualties and atomic failure retention are
  explicit. Server validation and BattleAI coverage remain engineering evidence.
- **L12 — Incorporated design:** Swift Gate arrives at the end of this round
  rather than the next round's start; Wide Gate changes radius 3 to 5; Hellfire
  Arrival divides 15% of arriving aggregate HP among adjacent enemies. Legacy
  implementation notes explain these same mechanics rather than adding a
  separate gameplay rule. The final audit verified its migrated disposition.
- **L13 — Incorporated:** Infernal Beacon explicitly lasts through the gated
  stack's first actionable round, so Swift Gate cannot consume it prematurely.
  Reserve Discipline and Endless Legion retain their accepted semantics.
- **L14 — Incorporated:** Reinforced Gate specifies floor rounding, temporary
  HP consumption before creature HP, no creature creation, no healing or
  resurrection of the pool, and battle-save persistence.
- **L15 — Incorporated:** Mobile Gate is one combined activation with floor
  rounding, legal path-distance limit, stationary use, complete-request
  validation and reserve retention after interruption or rejection.

### Primary growth entries L26 and L37

Compared the actual five-column class table, not only introductory prose.
All 18 class/faction/type identities and starting vectors agree with L37.
Every canonical growth vector equals its starting vector divided by five and
totals 20; every L37 growth vector totals 18. For example Knight is canonically
6/9/2/3 versus legacy 6/7/2/3; Wizard is 1/1/9/9 versus 1/1/8/8. The canonical
text explicitly adds independent 10%/20%/30% Skill chances for +1 attributes.
The user explicitly resolved this substantive conflict in favor of the new
DOCX model: retain 20 deterministic points, starting = 5 × growth, and its
specified Skill-based bonus rolls. This resolves design authority, not runtime
implementation or save migration; those still require verification.
L26's older ten-point formula was explicitly superseded by L37. Its separate
secondary-skill offer rules and Solmyr Wizard identity were compared and retained
in the canonical sections without restoring the obsolete formula.

### Linked Spell Points specification L25

Read NEW_HORIZONS_SPELL_POINTS.md in full, including its later event-driven
capacity rule, yellow presentation clarification, equipment transaction
boundaries, battle cancellation, save migration and temporary Hat access.
Intelligence, Hat, Reservoir, Buffer-pool, Magic Spring, ordinary restoration,
yellow UI annotation, equipment transaction boundaries, battle cancellation,
save normalization and temporary Hat access are now integrated into the
canonical DOCX. The linked file's old claim to override the DOCX has been
removed; its substantive content remains migration evidence.

### Final open-row audit — 2026-09-26

Two independent clause audits and an Astra review compared every remaining
entry against the DOCX rather than inferring completion from implementation.
Accepted missing presentation, initialization, logging, teaching, Order,
recruitment, Leadership, perk-browser, art-binding and forecast requirements
were integrated into their owning canonical sections.

The audit also resolved three stale contradictions without changing runtime
data: the old Frost Bolt Speed reduction is superseded by the detailed current
Ice Bolt rule and was not transferred to it; the generic three-counter action
panel is superseded by contextual Spell/Order opportunities and provider-driven
Skill status; and the old ten-point/fifty-weight formula is retired while the
authored class weight tables remain authoritative. School-rank UI wording now
governs acquisition only and cannot casting-lock a legitimately inscribed spell.
Art workflow, approval status and provenance remain enforced by
NEW_HORIZONS_DESIGN.md and the asset register, while their player-facing art
coverage and Transfigure Matter binding are now canonical.

## Entry coverage checklist

The following is the complete, resolved heading inventory. A design disposition
does not imply runtime implementation, native verification, visual acceptance, or
playable delivery; those remain tracked in the user-priority queue.

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

Every legacy entry now has a documented incorporated, reconciled, or superseded
disposition. The canonical DOCX and this audit retain the substantive decisions;
the legacy Overrides document is retired and must not be used as a runtime layer.
Implementation, native verification, visual acceptance and playable delivery
remain tracked independently in the user-priority queue.
