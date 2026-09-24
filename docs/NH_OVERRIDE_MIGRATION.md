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
`563c0a6e4fd3f33ffb8a7aecab443e74df531d300eb0f998f4085ba290d7daa6`.
The document is locally modified and has not been overwritten or included in
the transition edits. The original Downloads location is no longer available.
This is a text-level design audit, not a rendered-document layout review.

## Confirmed conflicts requiring review

| Legacy decision | Current canonical text / section | Conflict |
| --- | --- | --- |
| Perk ranks; Ordered Skill and perk progression | Skill and perk development: maximum three perks; explicitly not exactly one per tier; exceptional advancement bypasses rank prerequisites but not perk order | Legacy permits only one perk per rank and enforces rank prerequisites at every teacher. |
| Astral Nexus function | Unique buildings / Dungeon: each Nexus grants +5 permanent Knowledge once per hero | Legacy requires immediate refill to normal maximum. |
| Replace countered-spell perk | Tower / Metamagic: Spell Buffer restores 6 Mana when its Spell Action expires unused | Legacy replaces Spell Buffer with Spell Echo's +25% repeat-spell component. |
| Unified Normal/Buffer model / Intelligence | Wisdom / Intelligence: maximum Mana increases by 25% of Knowledge | Legacy specifies 130% capacity, separate Normal/Buffer pools, and related restoration rules. Full linked-spec audit still needed. |
| Inscribed spells and Spellbinder's Hat | Artifact conversion: Hat bypasses School proficiency for known spells and teaches none | Legacy grants castability to all inscribed spells without Hat; Hat supplies eligible Level 5 combat spells while equipped. |
| Passive Grand Metamagic | Tower / Metamagic: third trigger grants two Spell Actions | Legacy explicitly rejects an extra Spell Action replacement and defers a new design to the user. |
| Hero combat action panel | UI / Hero Action state: show contextual extensions of ordinary controls, not general action-token currency | Legacy requires separate Hero/Order/Spell remaining-count panel. May be reconcilable, but do not silently remove the panel. |
| Spellbook casting dialogs | UI / Magic Arrow Overcharge: panel beside cursor/card | Legacy explicitly centers the dialog. |
| Approved primary-attribute table | Primary Attributes / Starting Attributes and Growth: starting = 5 × growth; 20 deterministic points per level; Skill bonus rolls | Legacy uses 18 deterministic points, independent starting/growth vectors and no bonus rolls. All 18 starting vectors match; all 18 growth vectors differ. |
| Magi melee penalty | Tower creature rebalance / Mage and Arch Mage: “No melee penalty” | Legacy explicitly removes that ability from both creatures. |
| House of Wisdom | Unique buildings / Conflux and Experimental Values: Magic University sells Basic Magic School Skills for 5,000 Gold plus 2 of each precious resource | Legacy renames it House of Wisdom and sells randomly generated spell scrolls instead of Skills. |
| Arcane Reservoir | Unique buildings / Tower: once per week raise one visiting hero's current Mana to twice normal maximum | L25 explicitly replaces multiplication with +50 Buffer without refilling Normal. |

## Clause-level checks completed in this pass

### Gating entries L01, L11–L15 and L18

Canonical location: Inferno — Demonic Gating and its immediately following perk
pool. Related UI location: Faction Skill states. No entry in this group is yet
discardable solely on a shared perk name.

- **L01/L18 — Missing detail:** The canonical text requires a legal empty Gate
  hex and delayed arrival, but does not specify the reserved impassable full
  footprint, double-wide coverage, flame-only marker, no yellow outline, or
  successful-arrival-only Devil movement sound. These intended decisions need
  integration into the Gating placement/arrival and UI sections. The later L18
  clarification already superseded L01's original outline; preserve no outline.
- **L11 — Partially incorporated; missing detail:** Owned reserve troops outside
  the seven slots, Creature Activation cost, next-round arrival, Core/Elite/
  Champion rank access, and survivor return are explicit. Hero-screen deposit/
  withdrawal, transferring the whole selected reserve stack, and permanent
  casualties need explicit canonical treatment. Server validation and BattleAI
  coverage remain engineering acceptance obligations, not alternate design.
- **L12 — Incorporated design:** Swift Gate arrives at the end of this round
  rather than the next round's start; Wide Gate changes radius 3 to 5; Hellfire
  Arrival divides 15% of arriving aggregate HP among adjacent enemies. Legacy
  implementation notes explain these same mechanics rather than adding a
  separate gameplay rule. Eligible for migrated disposition after final audit
  verification; retained physically until the retirement gate passes.
- **L13 — Partially incorporated; timing clarification needed:** Reserve
  Discipline's negative-Morale floor in the arrival round and Endless Legion's
  victorious-only, gated Core/Elite, 50% rounded-down recovery are incorporated.
  Beacon says “first round on the battlefield,” whereas legacy specifies first
  actionable round. With Swift Gate those may differ. Flag that interaction for
  review before treating Beacon as migrated; do not choose silently.
- **L14 — Partially incorporated; missing detail:** Canonical Reinforced Gate
  grants 20% temporary aggregate HP until combat ends. Legacy additionally
  specifies floor rounding, consumption before creature HP, no new creatures,
  no healing/resurrection of this pool, and battle-save persistence. Integrate
  those semantics in the temporary-HP rule unless a conflicting global rule is
  identified; do not infer them from the percentage alone.
- **L15 — Partially incorporated; missing detail:** Half-Speed movement before
  Gating is present. The legacy combined single activation, floor rounding,
  path-distance limit, stationary option, complete-request validation, and
  interrupted-movement reserve retention are not explicit. Integrate the
  gameplay guarantees and retain validation tests as implementation evidence.

### Primary growth entries L26 and L37

Compared the actual five-column class table, not only introductory prose.
All 18 class/faction/type identities and starting vectors agree with L37.
Every canonical growth vector equals its starting vector divided by five and
totals 20; every L37 growth vector totals 18. For example Knight is canonically
6/9/2/3 versus legacy 6/7/2/3; Wizard is 1/1/9/9 versus 1/1/8/8. The canonical
text explicitly adds independent 10%/20%/30% Skill chances for +1 attributes.
This is a substantive conflict, not a stale heading to fix automatically.
L26's older ten-point formula was explicitly superseded by L37, but L26's
separate secondary-skill offer and Solmyr-class decisions still need comparison.

### Linked Spell Points specification L25

Read NEW_HORIZONS_SPELL_POINTS.md in full, including its later event-driven
capacity rule, yellow presentation clarification, equipment transaction
boundaries, battle cancellation, save migration and temporary Hat access.
Intelligence, Hat and Reservoir conflicts are confirmed above. Buffer-pool,
Magic Spring, ordinary restoration, UI and transaction semantics still need
their complete canonical clause search before integration. The linked file's
old claim to override the DOCX has been removed; its substantive content is
preserved intact as migration evidence.

## Entry coverage checklist

The following is the complete heading inventory, not a claim that the audit is
finished. “Open” means no removal or incorporation decision is yet justified.
Rows containing conflicts may also contain nonconflicting details still needing
verification. Do not treat a partial match as complete migration.

| ID | Legacy entry | Current disposition |
| --- | --- | --- |
| L01 | Reserved Gating arrival area | Missing placement, footprint and audiovisual details; see clause check |
| L02 | Replace Metamagic's countered-spell perk | Conflict above |
| L03 | Core, Elite, and Champion recruitment layout | Open |
| L04 | Astral Nexus function | Conflict above |
| L05 | House of Wisdom replaces Magic University | Conflict above |
| L06 | Solmyr Tower-versus-Inferno playtest profile | Open |
| L07 | Exhaustive meaningful battle logging | Open |
| L08 | Tower class and creature presentation | Open |
| L09 | Skill-development and action-help presentation | Open |
| L10 | Separate creature Speed and Initiative | Open |
| L11 | Demonic Reserve and Gating interaction | Partially incorporated; missing reserve/transfer details |
| L12 | First Demonic Gating perk tranche | Incorporated design; retained until final retirement verification |
| L13 | Second Demonic Gating perk tranche | Partial; Beacon first-actionable-round interaction flagged |
| L14 | Reinforced Gate temporary health | Partial; missing temporary-HP semantics |
| L15 | Mobile Gate combined action | Partial; missing combined-action guarantees |
| L16 | Magi melee penalty | Conflict above |
| L17 | Art workflow for provisional assets | Open |
| L18 | Pending Gate marker | Missing; later no-outline clarification controls L01's historical outline |
| L19 | Metamagic grants a round-long Spell Action | Open: round-long nonrecursive Spell Action incorporated; inspect allowance timing |
| L20 | Shared typed action system | Open |
| L21 | Hero combat action panel | Review conflict above |
| L22 | Fort category headings and Conflux layout | Open |
| L23 | Preserve accepted inscribed-spell casting rule | Conflict above |
| L24 | Different Orders coexist | Open |
| L25 | Unified spell access and Normal/Buffer Spell Points | Conflict above; include entire linked NEW_HORIZONS_SPELL_POINTS.md |
| L26 | Deterministic growth and secondary-skill offers | Earlier growth formula explicitly superseded by L37; class-weight/Wizard details still open |
| L27 | Perk ranks, missing choices, and empty slots | Progression conflict; presentation details still open |
| L28 | All skill-teaching sources use current learning rules | Open; compare exceptional advancement in canonical document |
| L29 | Hero initialization and stale spells | Open |
| L30 | Leadership displays | Open |
| L31 | Creature rank row and latest icon direction | Open |
| L32 | Skills probability control and familiar panel styling | Open |
| L33 | Spellbook inspection and casting dialogs | Placement conflict; other details open |
| L34 | Orders targeting, indication, animations, and art | Open |
| L35 | Meaningful logs and direct wording | Open; overlaps L07 |
| L36 | Art coverage and status tracking | Open; overlaps L17 |
| L37 | Approved replacement primary-attribute table | Conflict: all 18 growth vectors differ; saved-profile details still require comparison |
| L38 | Skill perk browser | Open |
| L39 | Ordered Skill and perk progression | Conflict above |
| L40 | Passive Grand Metamagic | Conflict above |
| L41 | Dynamic Overcharge outcome preview | Open: damage preview present; casualty comparison details need verification |

No legacy entry has been discarded. Retirement is not yet authorized by the
completion gate. Next: resolve flagged conflicts, finish clause-level comparison
of open rows, integrate missing decisions into the DOCX, verify the resulting
document, and only then retire legacy Overrides and update remaining references.
