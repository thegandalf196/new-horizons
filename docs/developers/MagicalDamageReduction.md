# New Horizons magical damage reduction

The defensive taxonomy and Experimental Values sections of
`docs/design-sources/New Horizons.docx` define this rule. This document records
the integration contract; it is not a claim that every path below is implemented.

## Arithmetic

For independent applicable reduction percentages r1 through rn, remaining
damage is the exact product of `(100 - ri) / 100`. Clamp that product to at least
`1 / 20`: aggregate Magical Damage Reduction cannot exceed 95%.

Relative penetration p then changes the remaining fraction to
`remaining + (1 - remaining) * p / 100`. Apply this fraction to incoming damage
and round down once. Do not round each source or the aggregate to whole percent.
Do not apply penetration separately to each source before combining them.

Examples with 100 incoming damage:

- Two independent 50% reductions leave 25 damage.
- Reductions of 50% and 20% combine to 60%; 15% penetration leaves 49 damage.
- A capped 95% reduction with 15% penetration leaves 19 damage after rounding.
- A 100% reduction source alone leaves 5 damage. It is not absolute immunity.

Magic Resistance, genuine spell/school immunity, and invincibility are separate
mechanics and must retain their own checks.

## Runtime boundaries

Enable the new aggregation through an explicit saved magic-rules opt-in, so a
loaded battle does not silently acquire different damage rules from installed
content. Ordinary spell effects, Lua previews, AI evaluation, and Fire Shield
must use the same reduction calculation. Remove reduction-as-immunity shortcuts
only for opted-in battles, including the separate Fire Shield shortcut.

Spellward contributes an independent 10% source for the target's current
controlling hero. Ownership queries must account for mind control, summoned and
gated stacks, and projected AI state. Do not infer control from original side.

Combat Casting supplies 15% relative penetration for hostile damage from an
ordinary empowered hero spell. Metamagic follow-ups and unrelated passive
effects do not inherit pending readiness. In the absence of a specified stacking
rule for multiple penetration perks, preserve the existing maximum-percentage
selection among eligible penetration effects.

Fire Wall and Land Mine must capture eligible penetration at creation. Their
later triggers recheck hostility and current target-side reduction. Serialize
the captured value, validate its range, default older payloads to zero, and
reject downgrades that would discard it.

## Acceptance

Verify independent-source aggregation, source-order invariance, cap before
penetration, exact rounding and overflow boundaries, true immunity, saved-rule
isolation, current-controller changes, Fire Shield, hostile and friendly hazard
triggers, snapshot save/load, and matching AI previews. Combat logs must describe
actual reduction or penetration for each affected target, not claim an effect
when a spell was resisted or counterspelled.
