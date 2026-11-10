# Combat Hero Actions and army Command

## Status and authority

This document describes the engine boundary for the current action rules in the
canonical [private design specification](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md).
The [save boundary](Serialization.md#current-save-boundary) requires a new game;
earlier saves are rejected without conversion or deletion.

## Core contract

Each hero receives the ordinary shared Hero Action for the round. A spell and an
Order compete for that action. Creature activations remain separate.

Command is the captured, unspent army-progression attribute. Orders consume the
ordinary Hero Action or an independently admitted typed allowance, with no Command
payment, exhaustion, refill or battle-start resource initialization. Slot unlocks
and enhancement receipts belong to hero progression, not the combat action ledger.

All heroes retain the ordinary Order roster; Warfare Skill availability does not
gate access to Orders. Battle Plan, Unbroken Ranks, Master Commander,
Decisive Orders and Heroic Spirit create no extra Hero Actions, mandatory
Order choices, precombat/reactive Order windows or creature-activation insertion.
Heroic Spirit contributes to the immutable issuance-time Order efficiency
snapshot; later Morale changes do not alter an issued Order.

## Authoritative transitions

Validation queries eligibility without mutating resources. The authoritative
simulation accepts a complete legal action before consuming its payment exactly
once. Canceling a dialog, canceling targeting, an invalid target or a rejected
request spends no action. An accepted spell that is subsequently countered has
still consumed its action and Mana payment.

Spell and Order controls, authoritative admission, replicated state and AI must
share the same eligibility and action rules. AI hypothetical states consume their
isolated action ledger, never live state. Cast histories remain histories and must
not be reconstructed as action budgets.

The typed allowance ledger retains action provenance, source restrictions and
expiry for explicitly supported native permissions. An enum or retained helper
cannot grant an opportunity by itself. Retired Double Command, Battle Plan and
Heroic Spirit action-grant states are rejected by current battle admission;
`OrderActionTransition` helpers do not override that boundary. Callers retain
staged state, publication and commit timing; server preparation and replicated
state validation remain independent trust boundaries.

Distinct active Orders coexist for their independent durations. Their benefit
lifecycle remains separate from the action ledger; expiry never refunds an action.

## Metamagic and presentation

Current Metamagic uses bounded hero points and does not create an additional
Spell opportunity. Point state and spell histories are distinct from permission
to cast. An automatic secondary spell likewise does not imply an extra manual
Hero Action or authorize an Order.

The hero-action panel displays shared Hero Action availability. Disabled Orders
explain actual action or target restrictions. Hero/army views display Command's
natural/Authority breakdown separately. Cancel remains available; the UI must not
force a retired follow-up chooser. Skill status uses the existing provider interface and selected
leather, red and gold presentation.

## Required validation

- A spell and an Order compete for the ordinary shared Hero Action.
- An accepted ordinary Order spends one shared Hero Action and no Command.
- Canceled or rejected attempts and previews spend nothing.
- Command does not gate otherwise legal Orders, spells or creature activations.
- Copies and descriptor roundtrips retain action expenditure and captured rules.
- Current profiles reject retired pending grants and activation-reordering state.
- Issued Order snapshots retain captured efficiency and ordinary expiry rules.
- AI projection agrees with authoritative allowances and never mutates live state.
- UI source checks complement native and rendered validation; neither substitutes
  for the other. Descriptor roundtrips do not establish running-battle health save.
