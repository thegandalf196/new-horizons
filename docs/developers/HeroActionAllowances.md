# Combat Hero Actions and finite Command

## Status and authority

This document describes the engine boundary for the current action rules in the
canonical [private design specification](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md).
The [save boundary](Serialization.md#current-save-boundary) requires a new game;
earlier saves are rejected without conversion or deletion.

## Core contract

Each hero receives the ordinary shared Hero Action for the round. A spell and an
Order compete for that action. Creature activations remain separate.

Each fighting hero also owns a finite battle-side Command pool. Battle entry
initializes remaining Command to its calculated maximum once. Every accepted
Order consumes exactly one Command in addition to its ordinary Hero Action.
Command never regenerates during combat. Capacity queries, deserialization and
hypothetical copies must preserve expenditure rather than rerun initialization.
At zero Command, otherwise legal spells and creature actions remain available.

All heroes retain the ordinary Order roster; Command Skill availability does not
gate access to Orders. The current Battle Plan, Unbroken Ranks, Master Commander,
Grand Marshal and Heroic Spirit effects create no extra Hero Actions, mandatory
Order choices, precombat/reactive Order windows or creature-activation insertion.
Heroic Spirit contributes to the immutable issuance-time Order efficiency
snapshot; later Morale changes do not alter an issued Order.

## Authoritative transitions

Validation queries eligibility without mutating resources. The authoritative
simulation accepts a complete legal action before consuming its payment exactly
once. Canceling a dialog, canceling targeting, an invalid target or a rejected
request spends neither Command nor the Hero Action. An accepted spell that is
subsequently countered has still consumed its action and Mana payment.

Spell and Order controls, authoritative admission, replicated state and AI must
share the same eligibility and payment rules. Command affordability is checked
before target enumeration or detailed Order projection wherever practical. AI
hypothetical states spend an isolated copy, never the live pool. Cast histories
remain histories and must not be reconstructed as action budgets.

The typed allowance ledger retains action provenance, source restrictions and
expiry for explicitly supported native permissions. An enum or retained helper
cannot grant an opportunity by itself. Retired Double Command, Battle Plan and
Heroic Spirit action-grant states are rejected by current battle admission;
`OrderActionTransition` helpers do not override that boundary. Callers retain
staged state, publication and commit timing; server preparation and replicated
state validation remain independent trust boundaries.

Distinct active Orders coexist for their independent durations. Their benefit
lifecycle remains separate from the spent Command point, which is never refunded.

## Metamagic and presentation

Current Metamagic uses bounded hero points and does not create an additional
Spell opportunity. Point state and spell histories are distinct from permission
to cast. An automatic secondary spell likewise does not imply an extra manual
Hero Action or authorize an Order.

The hero-action panel displays remaining/maximum Command and the shared Hero
Action separately. Disabled Orders explain exhausted Command or an unavailable
action. Spell controls use spell eligibility independently of Command. Cancel
remains available, including at zero Command; the UI must not force a retired
follow-up chooser. Skill status uses the existing provider interface and selected
leather, red and gold presentation.

## Required validation

- A spell and an Order compete for the ordinary shared Hero Action.
- An accepted Order spends exactly one Command and one ordinary Hero Action.
- Canceled or rejected attempts and previews spend nothing.
- Zero Command blocks Orders early without blocking legal spells or creatures.
- Copies and descriptor roundtrips retain spent Command without refilling it.
- Current profiles reject retired pending grants and activation-reordering state.
- Issued Order snapshots retain captured efficiency and ordinary expiry rules.
- AI projection agrees with authoritative resources and never mutates live state.
- UI source checks complement native and rendered validation; neither substitutes
  for the other. Descriptor roundtrips do not establish running-battle health save.
