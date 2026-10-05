# Bonus System

## Elemental final spell damage

`ELEMENTAL_SPELL_DAMAGE` grants a percentage bonus to final magical damage for
an explicitly tagged spell element. Subtypes `spellElementAir`, `spellElementFire`,
`spellElementWater`, and `spellElementEarth` are independent of Magic Schools.
The New Horizons Orbs use value 25. Untagged spells and physical damage do not
qualify; no School affinity supplies a fallback element. Equipment removal uses
ordinary bonus-tree cache invalidation, without a scan or new usage state.

## Explicit status tags

`Bonus::statusTags` records opt-in status classification. `DEBUFF` means the
author classified this applied status as a debuff; a negative value or low
Morale alone is not a tag. `statusIdentity` optionally distinguishes statuses
sharing a source/SID. Leave it empty for the ordinary existing identity.
Components of the same logical status should use the same identity. Generic
duration refresh preserves original numerical payload and caster provenance,
retains explicit classification and must not merge distinct explicit identities.
Tags travel with copied bonuses and their ordinary removal lifecycle. This is
metadata infrastructure, not an active effect counter or automatic tagging of
existing content. New Horizons Pandemonium's unresolved count/perk semantics
remain in the user-priority queue.

The bonus system of VCMI is a set of mechanisms that make handling of different bonuses for heroes, towns, players and units easier. The system consists of a set of nodes representing objects that can be a source or a subject of a bonus and two directed acyclic graphs (DAGs) representing inheritance and propagation of bonuses. Core of bonus system is defined in HeroBonus.h file.

## Bonus System Nodes

![Bonus System Nodes Diagram](../images/Bonus_System_Nodes.svg)

Legend:

- brown: actual nodes, and members of bonus system graph
- cyan: constant nodes that act only as source, and can not receive bonuses
- gray: virtual nodes to clarify graph layout. Actual node is located below. For example, there is no entity for "Visiting Hero", instead visiting hero is hero that is attached to player node only via Town node.

## Propagation and inheritance

Each bonus originates from some node in the bonus system, and may have propagator and limiter objects attached to it. Bonuses are shared around as follows:

1. Bonuses with propagator are propagated to "matching" descendants in the red DAG - which descendants match is determined by the propagator. Bonuses without a propagator will not be propagated.
2. Bonuses without limiters are inherited by all descendants in the black DAG. If limiters are present, they can restrict inheritance to certain nodes.

Inheritance is the default means of sharing bonuses. A typical example is an artefact granting a bonus to attack/defense stat, which is inherited by the hero wearing it, and then by creatures in the hero's army.
A common limiter is by creature - e.g. the hero Eric has a specialty that grants bonuses to attack, defense and speed, but only to griffins.
Propagation is used when bonuses need to be shared in a different direction than the black DAG for inheritance. E.g. Magi and Archmagi on the battlefield reduce the cost of spells for the controlling hero.

### Technical Details

- Propagation is done by copying bonuses to the target nodes. This happens when bonuses are added.
- Inheritance is done on-the-fly when needed, by traversing the black DAG. Results are cached to improve performance.
- Whenever a node changes (e.g. bonus added), a global counter gets increased which is used to check whether cached results are still current.

## Operations on the graph

There are two basic types of operations that can be performed on the graph:

### Adding a new node

When node is attached to a new black parent (the only possibility - adding parent is the same as adding a child to it), the propagation system is triggered and works as follows:

- For the attached node and its all red ancestors
- For every bonus
- Call propagator giving the new descendant - then attach appropriately bonuses to the red descendant of attached node (or the node itself).

E.g. when a hero equips an artifact, the hero gets attached to the artifact to inherit its bonuses.

### Deleting an existing node

Analogically to the adding a new node, just remove propagated bonuses instead of adding them. Then update the hierarchy.

E.g. when a hero removes an artifact, the hero (which became a child of the artifact when equipping it) is removed from it.

Note that only *propagated* bonuses need to be handled when nodes are added or removed. *Inheritance* is done on-the-fly and thus automatic.

## Limiters

`getUnstackedBonuses(selector)` exposes limiter-applied, updater-processed
effects before same-key stacking selection. Consumers that transform individual
effects must transform copies, then call `stackBonuses()` and `totalValue()`;
never mutate shared cached bonuses. The ordinary `getAllBonuses` query retains
its existing stacking semantics. Both views share the existing tree-version
cache invalidation, rather than introducing a periodic graph scan.

`Bonus::appliedByEnemy` records target-relative hostility at effect application.
It is not the dynamically computed `bonusOwner` used by propagated auras.
Duration-only refresh preserves the original effect value and provenance;
restoration preserves copied provenance, and later control changes do not
rewrite it. Missing legacy provenance defaults to false (unclassified), not a
guess based on negative values or the spell's polarity.

`Bonus::spellCasterOwner` separately records the stable owner of the effective
side that applied a spell effect. It is not inferred from `appliedByEnemy` or
the dynamic `bonusOwner`. When a spell effect is transferred to another target,
its origin remains unchanged and target-relative hostility can be recalculated
for the new recipient. Unknown legacy provenance remains
`PlayerColor::CANNOT_DETERMINE`. Ordinary duration-only refreshes retain the
stored owner; an effect-specific refresh that replaces its marker records the
new application owner.

If multiple limiters are specified for a bonus, a child inherits the bonus only if all limiters say that it should.

So e.g. a list of multiple creature type limiters (with different creatures) would ensure that no creature inherits the bonus. In such a case, the solution is to use one bonus per creature.

## Propagators

## Updaters

Updaters are objects attached to bonuses. They can modify a bonus (typically by changing *val*) during inheritance, including bonuses that a node "inherits" from itself, based on properties (typically level) of the node it passes through. Which nodes update a bonus depends on the type of updater. E.g. updaters that perform updates based on hero level will update bonuses as the are inherited by heroes.

The following example shows an artifact providing a bonus based on the level of the hero that wears it:

```json
   "core:greaterGnollsFlail":
   {
       "text" : { "description" : "This mighty flail increases the attack of all gnolls under the hero's command by twice the hero's level." },
       "bonuses" : [
           {
               "limiters" : [
                   {
                       "parameters" : [ "gnoll", true ],
                       "type" : "CREATURE_TYPE_LIMITER"
                   }
               ],
               "subtype" : "primSkill.attack",
               "type" : "PRIMARY_SKILL",
               "val" : 2,
               "updater" : "TIMES_HERO_LEVEL"
           }
       ]
   }
```

# Fractional magical damage reduction

Non-spell magical abilities use `newHorizonsMagicalAbilityDamage::adjustDamage`.
For saved New Horizons multiplicative-MDR battles it combines independent
ANY-school percent/basis-point sources, Hold the Line and controller-derived
perk protection with the ordinary magical reduction cap. This is damage
mitigation, not a spell cast: it does not invoke Magic Resistance, School or
spell-specific immunity, Spell Lock, caster bonuses or elemental Orb bonuses.
Outside that saved profile it leaves the ability's raw damage unchanged; it
does not alter any existing legacy spell path.

`SPELL_DAMAGE_REDUCTION_BASIS_POINTS` stores an independent reduction source
in basis points: 100 equals 1%, and values are bounded to 0–10000 at damage
resolution. Its subtype follows `SPELL_DAMAGE_REDUCTION` (a Spell School or
`any`). New Horizons magic-rules v3 combines these sources multiplicatively
with percent-based reduction sources; it does not sum their percentages.
Timed spell applications use ordinary `SPELL_EFFECT` source IDs and `N_TURNS`
lifetimes, so refresh and Dispel use the existing spell-bonus lifecycle.
Legacy rule snapshots retain their original damage-reduction behavior.

## Fractional physical damage reduction

`PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS` stores an independent physical
reduction source: 100 equals 1%. The captured New Horizons physical mitigation
stage groups bonuses by source type and source ID, retains each group's bonus
value-type rules, and multiplies distinct sources with the other explicit
physical reductions. The captured global physical cap applies after stacking;
this bonus does not bypass it. Creature Defense is a separate earlier layer.
It affects melee and ranged physical damage, not magical damage or spell
targeting. Legacy profiles without that mitigation stage do not consume it.

Timed spells use ordinary `SPELL_EFFECT` source IDs and `N_TURNS` lifetimes;
recast/Dispel therefore use the existing effect lifecycle. The append-only type
requires `NEW_HORIZONS_SHIELD_OF_CHAOS_PHYSICAL_REDUCTION` serialization support;
down-saving its state to an older format is rejected.

## Hypnotize cast-time ceiling metadata

The `HYPNOTIZED` marker may carry a named `addInfo.maximumTargetHealth`
integer. The authoritative spell-effect recorder captures the exact
target-adjusted ceiling used by the original Hypnotize cast. This metadata does
not change control or targeting; it lets later effect-transfer rules evaluate
the original cast limit without re-running caster bonuses. A duration-only
refresh preserves the existing marker payload, matching normal `toUpdate`
semantics. Markers without the payload remain valid for older states. The
structured payload is stored in `BonusParameters` as a `JsonNode` and is
validated as a non-negative integer when parsed.

## Favorable creature probability modifiers

`FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS` multiplies explicitly
classified favorable random creature procs. 10000 means unchanged probability;
use `INDEPENDENT_MIN` for a timed penalty. The shared
`favorableCreatureAbilityChanceBasisPoints(basePercentage)` getter returns
basis points, preserving fractional percentages. Zero chances stay zero and
deterministic abilities (base chance at least 100%) stay deterministic.
This is not a global RNG modifier: hero-owned machine bonuses, harmful Fear
rolls and random selection among deterministic outcomes are not consumers.

Authoritative percentage-biased rolls use `rollFavorableCreatureAbility`.
Fractional percentages are stochastically rounded before the existing roll;
the persistent bias stream retains its percentage-sized dice and unchanged
chances retain their original RNG path. Binomial mechanics such as Death Stare
instead consume the exact basis-point probability directly.

`MAXIMUM_LUCK` caps final attack Luck, including positive Luck transformations.
A zero cap prevents positive Luck without removing negative Luck. It also
prevents the Perfect Moment forced-positive-Luck action. Timed spell markers
use the existing spell-effect expiry and Dispel lifecycle. Both bonus types
require serialization feature `NEW_HORIZONS_CREATURE_PROBABILITY_MODIFIERS`;
down-saving either to an older format is rejected rather than losing state.
