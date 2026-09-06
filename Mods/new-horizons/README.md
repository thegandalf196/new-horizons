# Curated New Horizons content

SPDX-License-Identifier: GPL-2.0-or-later

This is internal edition content, not a player-managed mod collection. Runtime
artwork is original CC0 geometry; editable sources and provenance are in
`assets/new-horizons/`. Referenced Heroes III resources remain purchaser-supplied.

## Authored six-school increment — integration pending

`tools/update-new-horizons-module.py` generates metadata from canonical
`config/newHorizons{Combat,Magic,Schools,Skills}.json`. Do not edit the inline copy.
The six-school increment requires its matching runtime, serialization and UI;
source data or image generation alone is not a gameplay acceptance result.

The magic snapshot classifies all 69 existing common hero spells. It does **not**
register fictional versions of the planned new spell roster. Titan's Bolt and
creature-only abilities/triggers retain their special handling. Existing spells
keep their real names/effects: Magic Arrow is not falsely labeled a completed new
Magic Missile implementation. New spells/effects, qualitative masteries, growth,
secondary attributes, remaining commands and creature tiers are subsequent work.

Six new school skills provide actual Basic/Advanced/Expert school bonuses, with
provisional acquisition weights Might 2 / Magic 6. These ranks are not the later
one-of-three post-Expert mastery choices. New-game starting skill migration is
explicitly provisional: Air to Sorcery, Fire to Havoc, Water to Light and Earth to
Nature. Saved legacy games must not receive that conversion or new eligibility.

Where the design explicitly gives an existing spell's tier, the snapshot supplies
that tier and provisional costs by rank (none/basic/advanced/expert):

| Tier | Costs |
| --- | --- |
| 1 | 4 / 4 / 3 / 3 |
| 2 | 8 / 8 / 6 / 6 |
| 3 | 12 / 12 / 10 / 10 |
| 4 | 16 / 16 / 13 / 13 |
| 5 | 22 / 22 / 18 / 18 |

Other existing spells retain their original tier/cost definitions for this
increment. Scaled primary attributes and spell-specific power coefficients are
not established by these cost overrides. Numerical balance remains deferred.

## Deliberately visible provisional choices

- Necropolis uses Shadow/Chaos provisionally; the supplied table instead listed
  Shadow/Sorcery. Fortress uses Nature/Shadow provisionally because its table's
  minor-school cell was blank. Both records carry `provisional: true`.
- Blind belongs to **both Shadow and Chaos** for now: its list placement and the
  school-description prose disagree. This is not a settled exclusive assignment.
- Existing elemental summons belong to Nature and Havoc. The original elemental
  affinities remain relevant to existing artifacts/protections; classification
  does not globally rewrite the underlying spell objects.
- Mage Guild major/minor weights are provisionally 3/1. Authored obligatory spells
  and legacy saves require their existing compatibility behavior.
- Remaining existing-spell assignments are provisional semantic mappings, not a
  claim that the supplied document enumerated every original spell.

New games store versioned rules; installed metadata must not reinterpret saved
worlds. Ordinary preset activation, actual native rules and saved-state checks,
AI behavior, and independent rendered/input journeys are separate required gates.
