# Warcasting integration contract

The detailed Warcasting section in `docs/design-sources/New Horizons.docx`
defines the gameplay rule. This document describes its engine boundaries; it
does not replace the source or claim that every Warcasting perk is implemented.

## Rules and state

New Horizons magic rules v2 may explicitly set `warcasting: true`. Missing or
false means that the alternating-action mechanic is disabled. The setting is
part of the saved rules, not inferred from newly installed content.

Basic, Advanced, and Expert grant 10%, 20%, and 30% respectively. An accepted
ordinary hero Spell arms the next Order; an accepted Order arms the next Spell.
Readiness lasts through the end of the following round. Repeating an action
does not consume an opposite-action bonus, but refreshes the readiness it arms.
Metamagic follow-ups are continuations of the same Hero Action, not new actions.
Rejected requests must leave readiness unchanged. A valid spell negated by
Counterspell still spends its Hero Action.

`AlternatingHeroActionState` holds direction, percentage, and inclusive expiry.
Queries do not mutate it. Authoritative accepted-action processing owns state
transitions; client widgets and AI evaluation only read or simulate them.
Live battle serialization must be versioned, with inert defaults for older
saves and rejection of downgrades that would discard an active mechanic.

## Numerical boundaries

An empowered Order adds percentage points to the efficiency of its
attribute-derived terms. It does not multiply the fixed base, movement-distance
extras, or number of flanking sides. Its consumed bonus must be captured with
the issued Order so delayed attacks do not read newly armed readiness.

An empowered Spell increases its Spell Power-derived numerical component.
Fixed damage, healing, or health-pool terms are unchanged. Apply the percentage
before rounding the component where the formula permits it. Do not globally
increase the caster's Spell Power: that would also change durations, targeting
radii, unlock thresholds, and obstacle-count thresholds.

AI previews, actual effects, and the combat log must agree on the captured
bonus. Logs may describe a consumed empowerment, but must not claim an effect
landed when Counterspell suppressed it.

## Hero status

The battle hero panel should show readiness only when it actually exists:
the next eligible action, the percentage, and expiry. This is an effect
indicator, not an additional action currency. It must coexist with the
Counterspell ward without overlapping mana or creature panels.

## Validation

The pure-state suite covers alternation, refresh, expiry, non-mutating queries,
invalid-input atomicity, overflow, and serialized shape validation. Full runtime
acceptance additionally requires accepted/rejected action tests, fixed-versus-
attribute component math, Metamagic exclusion, save/load continuation, AI
simulation, and visual confirmation of the hero panel.

Perks require their own runtime and regression coverage before their registry
entries may be changed from `planned` to `active`.

## Directional perks

Martial Channeling adds 10 percentage points only when a Spell arms the next
Order. Arcane Channeling adds 10 percent only when an Order arms the next Spell.
The percentage is captured when readiness is armed, so later changes do not
silently alter an already issued Order or an in-flight spell calculation.

Tactical Weaving extends the inclusive expiry to the end of round R+2, where R
is the round in which readiness was armed; without it the expiry is R+1.
It does not grant another Hero Action or change Metamagic follow-up eligibility.

Readiness icons identify the next action, not the hero's mastery rank. Perks can
increase the amount without increasing mastery, so the UI must never infer
Basic, Advanced, or Expert from the displayed percentage.
