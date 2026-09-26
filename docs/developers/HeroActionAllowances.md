# Contextual combat action opportunities

## Status and authority

This document describes the engine boundary for the action rules in the
canonical [New Horizons.docx](../design-sources/New%20Horizons.docx). The legacy
three-counter allowance proposal was superseded during the completed
[Overrides migration](../NH_OVERRIDE_MIGRATION.md). This is not a second gameplay
specification and does not claim that every consumer is already migrated.

## Core contract

Each hero receives the ordinary shared Hero Action for the round. That action
may pay for either one spell or one Order. Creature activations are separate and
never consume the hero's action.

Some mechanics create a contextual opportunity to perform a particular action,
such as Metamagic permitting another spell or a perk permitting a different
Order. These opportunities are source-specific permissions, not a general token
currency and not three independent Hero/Spell/Order counters. They must retain:

- their source and eligibility restriction;
- their expiry and any once-per-combat budget;
- the provenance of the accepted action that created or consumed them;
- authoritative serialization when they can cross a save boundary.

An opportunity never authorizes out-of-turn input. Existing side control and
activation-window rules still apply. A restricted spell opportunity cannot issue
an Order, and an Order opportunity cannot cast a spell.

## Authoritative transitions

Validation queries eligibility without mutating state. Consumption occurs only
when the server accepts the complete action. Canceling a dialog, choosing an
invalid target, or receiving a rejected request spends nothing. An accepted spell
that is subsequently countered has still used the opportunity that paid for it.

All consumers must use the same authoritative eligibility query: spell and Order
controls, server validation, replicated state, AI simulation and execution,
combat logging, and save/load. Cast histories remain histories; they must not be
reconstructed as action budgets. Stable source identity and expiry resolve any
case in which more than one contextual permission could pay for the same action.

Distinct active Orders coexist for their normal durations. Issuing a second
different Order does not replace the first, and no grant may create a recursive
extra-action loop unless a canonical rule explicitly says so.

## Metamagic

When the ordinary Hero Action is used to cast a spell, Metamagic may open one
additional Spell opportunity according to its current rank, sequence budget, and
perk restrictions. It lasts until the end of the current round, does not force an
immediate follow-up, cannot issue an Order, and cannot itself trigger another
ordinary Metamagic grant. Creature movement, attacks, Wait, and Defend remain
legal while it is available; the spellbook does not reopen automatically.

The sequence use is charged when the additional spell is actually accepted, not
merely when the opportunity becomes available. Formula Reserve, Spell Buffer,
Grand Metamagic, and other source-specific behavior remain separate canonical
rules; do not generalize one source's timing or eligibility to all opportunities.
Spell-sequence metadata must remain distinct from the permission to cast.

## Presentation

The hero battle panel presents the ordinary Hero Action and any currently usable
contextual spell or Order opportunity in the established leather, red, and gold
style. It must not imply that every hero owns three refillable counters. Generic
provider-driven Skill status appears beneath the action state, so Metamagic points
and future faction resources can share the same UI without a hardcoded
Metamagic-only field.

Help text identifies the source, eligible action, remaining uses where relevant,
and expiry. Controls remain the ordinary spellbook and Order controls rather than
inventing a replacement Wait button or automatically opening a modal. The compact
Overcharge/casting modal is centered, but its placement does not change action
semantics.

## Required validation

- A spell and an Order compete for the single ordinary Hero Action.
- A contextual Spell opportunity cannot issue an Order, and vice versa.
- Intervening creature actions leave an unexpired Metamagic opportunity usable.
- Expiry, round refresh, once-per-combat use, and save/load occur exactly once.
- Canceled or rejected actions spend nothing; accepted countered spells do spend.
- Additional casts do not recursively trigger ordinary Metamagic grants.
- Distinct Orders can coexist for their normal independent durations.
- AI projection and execution agree with authoritative eligibility and cannot loop.
- UI status and logs reflect accepted transitions and their actual source.
- Old saves migrate explicitly; unrepresentable active state fails visibly.
- Warcasting, Battle Meditation, Time Stop, Counterspell, and every implemented
  source-specific opportunity have focused provenance and expiry regressions.
