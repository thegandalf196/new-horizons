# Typed combat action allowances

## Status

Migration design, not a claim of implemented behavior. The accepted rules are in
[New Horizons overrides](../NEW_HORIZONS_OVERRIDES.md). Existing immediate
Metamagic sequence enforcement must be replaced across all consumers together.

## State contract

Keep the performed action (spell or Order) distinct from the allowance paying
for it (Hero, Spell, or Order). Hero allowances can pay for either kind; typed
allowances can pay only for their matching kind. Creature activations are a
separate resource and do not consume any of these allowances.

Use one authoritative, serialized allowance ledger per battle side. Grants need
their source, permitted action kind, expiry, and any source-specific eligibility
restriction. This supports restricted grants such as a Light-only spell or a
different Order without treating them as unrestricted extra Hero Actions.
Source-specific once-per-combat budgets remain distinct from spendable grants.

Validation selects an eligible allowance without mutating state. Commit its
consumption only when the action is accepted. A rejected target or canceled
dialog spends nothing; an accepted spell that is countered still spends its
allowance. Record the spent allowance and grant source with the accepted action
so perks and logs do not reconstruct provenance from spell-count heuristics.

Prefer an eligible specialized allowance before the flexible Hero allowance.
Do not spend a restricted grant on an ineligible spell or Order. All UI and AI
predictions must use the same eligibility query as authoritative validation.

Possessing an allowance does not authorize out-of-turn input. Preserve existing
side-control and activation-window validation; a round-long lifetime alone must
not allow interruption of an opponent's activation. Determine any additional
end-of-round action window explicitly rather than introducing it implicitly.
Use expiry and stable grant identity to break ties between equally specialized
eligible grants. Sidebar counts report unexpired grants, not whether the currently
selected target satisfies a grant's restrictions.

## Metamagic migration

An accepted spell paid for with the round's Hero Action can grant a Spell Action.
That grant lasts through the current round and does not force immediate use.
Movement, attacks, Wait, and Defend must remain legal while it is available.
The spellbook does not reopen automatically, and Wait retains its normal role.
A spell paid for with a Spell Action is not another Hero Action for grant
triggers. Preserve spell-sequence metadata needed by Metamagic perks separately
from the allowance that permits the next cast.

Combat-use charging on grant versus actual cast is not yet resolved. Preserve
the existing actual-cast charging behavior until that choice is confirmed.
Outstanding grants must reserve their source's remaining combat capacity so
deferred charging cannot create more grants than the source permits.
Grand Metamagic and source-specific immediate grants require an explicit audit;
do not silently generalize the round-long override to every perk's timing.

## Presentation and consumers

The hero sidebar displays separate remaining Hero, Order, and Spell Action
counts below Morale/Luck. A flexible allowance is counted once, under Hero.
Expose counts through the read-only battle callback, not client-maintained
counters. Describe restricted grants and expiry in help text.

Server validation, replicated state application, spell and Order availability,
AI hypothetical battle state, action selection, combat logs, and save/load must
all use the shared contract. Cast histories remain histories, not substitute
action budgets. Warcasting must distinguish triggers requiring a Hero Action
from effects consumed by a subsequent spell or Order.
The same provenance audit must cover Battle Meditation, Time Stop expiry,
Counterspell wards, and Metamagic sequence perks. An old saved pending Metamagic
sequence must migrate to its corresponding Spell allowance and retain its
sequence metadata; both expire at the appropriate round boundary.

## Required validation

- A spell and an Order compete for the single ordinary Hero Action.
- A Spell Action cannot issue an Order; an Order Action cannot cast a spell.
- Intervening creature actions leave a Metamagic Spell Action available.
- Expiry and round refresh occur once and survive save/load correctly.
- Canceled/rejected actions spend nothing; accepted countered spells do spend.
- Specialized grants do not create recursive Hero Action triggers.
- AI simulation and execution spend identical allowances and cannot loop on
  unavailable casts or Orders.
- Sidebar counts and logs reflect accepted authoritative transitions.
- Old saves migrate explicitly; exporting unrepresentable state fails visibly.
- Each existing active grant and each newly implemented perk has a regression
  covering its restrictions, budget, expiry, and interaction with other grants.
