# Spell access and Spell Points

Accepted user specification, 2026-09-23. This document overrides conflicting
scripture passages only for the rules below. Referenced by
[NEW_HORIZONS_OVERRIDES.md](NEW_HORIZONS_OVERRIDES.md).

## Inscribed combat spells

A combat spell legitimately inscribed in a hero's spellbook may be cast
regardless of School proficiency. This applies to every hero, including starting
spells above the hero's normal proficiency. Proficiency governs acquisition and
access, not permission to cast an already-inscribed spell.

This rule grants no unknown spells, restores no removed or unavailable spells,
and does not bypass Spell Point costs, typed action requirements, targeting,
immunities, Spell Lock or other ordinary casting restrictions. Spells normally
compete with Orders for the Hero Action; specialized action grants retain their
own restrictions. Adventure Spell rules remain separate.

## Spellbinder's Hat

The Hat is redesigned around availability, not a proficiency-lock bypass:

> While equipped, all Level 5 combat spells are inscribed in the hero's spellbook.

Eligibility still excludes removed/unavailable spells, Adventure Spells and
hero-specific exclusions. Equipment-provided access must be distinguishable from
independently learned spells: removing the Hat ends its access without erasing
legitimately learned spells. This supersedes the older Hat no-spell-grant rule.

## Two pools, one currency

Internally track Normal Spell Points and Buffer Spell Points separately:

`Total Available Spell Points = Normal Spell Points + Buffer Spell Points`

The hero has one ordinary Maximum Spell Points value:

`0 <= Normal Spell Points <= Maximum Spell Points`

Buffer Spell Points may exceed that maximum and are ordinary spendable Spell
Points, not another currency. Spell costs spend Buffer first, then Normal.
Granting Buffer does not refill missing Normal Spell Points. Ordinary restoration
does not consume, replace or refill Buffer.

Example: 30 Normal, maximum 50, then +50 Buffer gives 80 total. An ordinary full
refill subsequently gives 50 Normal + 50 Buffer = 100 total. Reversing those two
operations produces the same result.

## Knowledge, Intelligence and equipment

Without Intelligence:

`Maximum Spell Points = effective Knowledge`

With Intelligence, use the provisional balance value:

`Maximum Spell Points = floor(1.30 * effective Knowledge)`

Knowledge from artifacts counts toward effective Knowledge. Increasing capacity
never restores the newly created empty capacity. Decreasing capacity immediately
clamps Normal Spell Points to the new maximum; lost points do not return when
capacity increases again. Buffer is unaffected by capacity changes.

Examples:

- 80/100, equip +20 Knowledge: 80/120, not 100/120.
- 120 Normal at maximum 120, remove +20 Knowledge: 100/100. Re-equipping gives
  100/120, not 120/120.
- With Intelligence, 100 Knowledge gives maximum 130; +20 Knowledge raises it
  to 156. Removing the artifact returns the maximum to 130 and clamps Normal.
- 120 Normal + 50 Buffer at maximum 120, remove +20 Knowledge: 100 Normal +
  50 Buffer at maximum 100, displayed as 150/100 with +50 Buffer.

Intelligence is the sole general multiplicative maximum-capacity effect unless
a later explicit rule says otherwise. Other effects modify Knowledge, restore
or regenerate Normal points, reduce costs, or grant Buffer. Do not preserve the
old equipment-swapping over-capacity behavior as an implicit Buffer grant.

## Restoration and overcharge sources

Provisional values remain tunable; their pool semantics are mandatory.

- **Arcane Reservoir (Tower):** replaces the old Mana Vortex multiplication;
  grants +50 Buffer, without refilling Normal, changing Knowledge or changing
  maximum capacity.
- **Magic Spring:** restores Normal to maximum and grants +25 Buffer. Never
  doubles the current pool or maximum. Use this combined refill-and-buffer
  variant unless a later instruction selects the buffer-only alternative.
- **Ordinary restoration:** town/Mage Guild rest, Magic Wells, the Dungeon
  immediate-refill building, ordinary restoration effects, regeneration and
  Mysticism-style effects affect Normal only unless explicitly granting Buffer.

For example, visiting the Reservoir at 60/100 produces 60 Normal + 50 Buffer,
displayed as 110/100 with +50 Buffer. A later ordinary refill gives 150/100 with
the same +50 Buffer. Existing source visit/usage restrictions are not implicitly
removed by changing the resource model.

## Presentation and integration

Keep a familiar total/maximum display with a distinct Buffer annotation:

`310 / 460   +50 Buffer`

Here 310 is total spendable points, 460 is normal maximum, and the +50 is already
included in 310: the internal Normal amount is 260. Never add the annotation a
second time. The expanded tooltip explains total, maximum, Buffer, and Buffer-
first spending. Use the required HoMM3 art workflow for any new icon artwork.

The approved visual direction uses the familiar brown textured panel and gold
border/title, white total/maximum, and a bright blue sparkle with a blue `+50`
Buffer annotation. The expanded tooltip repeats the numeric line, separates it
from the explanation with a gold divider, and highlights the Buffer amount and
name in blue. The supplied image is a layout reference, not an automatically
licensed runtime art asset.

Server validation, spell spending, equipment changes, restoration, AI valuation,
battle/adventure UI and save/load must use the same model. Save compatibility
must be explicit; a legacy single-pool number is not proof of how its excess was
originally obtained.

The governing distinction is: Knowledge defines capacity; Intelligence improves
capacity; restoration fills capacity; Buffer effects exceed capacity.

## Integration acceptance checks

The pool value type alone does not implement this system in gameplay. Verify all
of these paths before promoting a build with the new model:

- Spell costs and Mana drains consume Buffer before Normal, including exact
  exhaustion and insufficient-funds rejection without partial mutation.
- Restoration and Buffer grants commute as described above; AI distinguishes
  total casting resources from missing Normal capacity.
- Equipment swaps reconcile capacity after the complete authoritative operation,
  not after intermediate removal steps. An equal-capacity swap preserves Normal;
  a real capacity reduction loses excess Normal without touching Buffer.
- Knowledge, Intelligence, inherited bonuses and level changes all update the
  same capacity contract, on both simulation and client state.
- Battle cancellation/retry restores the original pair of pools. A completed
  battle preserves legitimate restoration and persistent Buffer grants, while
  expiring any unused combat-only energy. Do not cap total or Buffer against its
  pre-battle value: that would discard points legitimately gained during combat.
- Battle saves and adventure saves round-trip both pools; legacy scalar Mana
  migration is explicit and never manufactures Buffer from an unknown source.
- Spellbinder's Hat supplies temporary eligible Level 5 combat access without
  writing permanent knowledge; removing it preserves independently learned spells.
- All total/maximum readouts and tooltips agree on the Buffer-inclusive total;
  they neither add Buffer twice nor disclose hidden enemy information.

## Implementation checkpoint — 2026-09-23

The native client and test binary build successfully. The final focused run
passed 66 tests covering pools, artifact capacity changes, rewards, hero/battle
serialization and legacy scalar reading, Hat access and AI valuation, real
Wraith Mana Drain, and rejection of malformed drain/BattleStart packets without
partial state changes. A broader 146-test combat/AI run passed before the final
two rejection-path hardening changes; the focused run includes those changes.
The 68 selected Python content/configuration checks also passed.

These are scoped automated checks, not full gameplay or graphical acceptance.
The compact hero tooltips have compiled Buffer readouts, but rendered spacing
and long-value clipping remain unverified. The requested blue sparkle artwork
is not installed. Snapshot startup validation and promotion are separate gates.
