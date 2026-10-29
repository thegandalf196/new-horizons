# Living Inferno runtime contract

Fiur's optional captured `livingInfernoSpecialty` selects the Fire/Energy
Elemental movement-trail specialty. Absence preserves the historical creature
specialty; loading never fills a missing capture from installed settings.
The object contains version 1, `damagePerCreature` and `durationRounds`.
Coefficients are balance data. The raw damage snapshot is the current source
count multiplied by `damagePerCreature`, saturated to the obstacle's integer
range. It is not multiplied by Spell Power or hero spell Luck.

## Committed movement

The server collects only ordinary voluntary movement that actually commits.
Rejected destinations, deployment, teleportation, forced displacement and
return-after-attack movement do not create a trail. The walking path comes
from existing committed movement packets, including interrupted prefixes.
The final occupied footprint is excluded, including both hexes of a wide unit.

Energy Elementals fly. Native flying packets contain the destination only and
presentation travels directly between the two screen positions. The shared
trail helper projects that straight segment with integer rational cube
interpolation, sampling once per hex distance. At equal squared cube distance,
the lowest native hex index wins. This rule is deterministic under reversal,
uses no RNG or obstacle search, and does not change movement legality.
Ground movement never substitutes this line for its actual committed path.
Physical footprint orientation stays on the original unit side; specialty and
passive caster ownership use the existing allegiance-owner hero query. This does
not turn an action controller into an owner.
The AI reuses its existing voluntary movement path and the same helper;
generic hypothetical `moveUnit` remains a forced/spell displacement primitive.

## Damage, lifetime and presentation

One move produces at most one logical `SpellCreatedObstacle`, with the normal
Fire Wall identity, trigger, immunity/reduction rules and activation deduplication.
Existing Fire Wall hexes are omitted, preserving their damage and expiry.
The trail can affect either army. It spends no Mana or Hero Action.
`turnsRemaining` includes the issuing round and uses existing obstacle round
expiry. The damage snapshot remains unchanged if the source later loses troops.

Only this producer sets `renderAtEachHex`. Its default is false; historical
obstacles and ordinary Fire Walls retain anchor-only presentation. A marked
obstacle uses the existing single-hex flame at each affected hex for appearance,
idle playback and removal. This flag creates no additional damage sources.
The native flame is 44 pixels wide; no sprite scaling or new artwork is needed.

## Compatibility and verification

`NEW_HORIZONS_LIVING_INFERNO` guards both the captured hero policy and the new
obstacle presentation field. Unsupported writers reject a marked obstacle
before its prefix; old readers/load paths retain default-false presentation.
The feature must be appended after existing save capabilities and before aliases.
Existing binary obstacle copies and JSON obstacle updates retain the flag.

Focused tests cover integer flight geometry, footprint exclusions, current and
historical serialization, native movement, normal Fire Wall trigger reuse and
isolated AI projection. Compilation does not establish rendered placement,
expiry animation or balance; those require a witnessed game run.
