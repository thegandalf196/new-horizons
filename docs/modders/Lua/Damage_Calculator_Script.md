# Damage Calculator Script

Declared with `"implements" : "damageCalculator"` in the [scripts](Script_Types.md) section of a mod. This script decides what an attack is worth - the damage of the creatures, everything that raises or lowers it, and estimation on how many creatures die.

Unlike the other script types there is exactly **one** damage calculator in a game. It is not attached to a unit and nothing grants it: the engine asks it about every attack, whether the blow is being dealt or an AI is only weighing it. VCMI ships `core:damageCalculator` with a fixed core patch chain.

```json
"damageCalculator" : {
    "implements" : "damageCalculator",
    "script" : "damage/damageCalculator",
    "patches" : [ "damage/siegeWeapon", "damage/magicElemental", ... ],
    "schema" : { "properties" : {}, "additionalProperties" : false }
}
```

## How damage is worked out

Every attack goes through the same three steps.

**1. Base damage.** What the creatures themselves deal, min and max, multiplied by how many of them are alive. Bless and curse collapse that range onto one of its ends, a ballista multiplies it by the attack of its hero.

**2. Factors.** Everything that scales base damage is a *factor* - a signed share of the base damage. **Positive raises it, negative lowers it.** What decides how a factor applies is its sign alone, not where it came from:

- factors that raise the damage **add up**: attack over defence (+5% per point), offence (+30%), luck (+100%) give `1 + 0.05×points + 0.3 + 1.0`
- factors that lower it **multiply**, each taking its share of what is left: armourer (-15%) and a shooting penalty (-50%) give `0.85 × 0.5`

The two totals are multiplied together. This is why a single -50% never quite halves the damage twice, and why giving a "boost" a negative value turns it into a mitigation rather than cancelling out other boosts.

**3. Casualties.** How many creatures the resulting damage kills, given the health left on the first one. This is only used for damage preview in UI, and for AI estimation - engine instead rolls damage within specified range.

## New Horizons physical reduction stage

Saved New Horizons magic rules may opt into independent physical reductions with
`physicalDamageReductionCapPercent` (integer 0–100; current content uses 80).
The corresponding attack payload uses `-1` when the setting is absent, preserving
historical damage calculations. The setting is valid only in magic rules v2.

For opted-in physical attacks, explicit reduction sources multiply separately,
then their combined reduction is capped before final damage rounding. Creature
Defense, petrification, range/obstacle/melee penalties, outgoing damage penalties,
and Phantom Army's damage profile remain outside this cap. Armorer, Battlecraft
Defend, Bulwark, defensive Orders and applicable Shield-type effects contribute
to this stage instead of also contributing to the old reduction factors.

Generic reduction bonus contributions are grouped by source and source ID within
their applicable subtype; each group's `totalValue()` supplies one coefficient.
This preserves ordinary base/percentage contributions to one named effect.
Cross-source bonus modifiers are not a supported content pattern for this
curated stage; third-party content relying on them should retain the historical
path until it defines an explicit reduction-source contract.

## Implementation

The engine implements the calculator and its core patch chain natively, as
described in [script types](Script_Types.md#implementations). It captures the
attack's owned facts first and then runs the pure numeric calculation; see
[fixed native rules](../../developers/Native_Rules.md). New Horizons does not
execute authored calculator patches.
