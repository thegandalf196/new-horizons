# Dread Cavalry continuation contract

The optional captured `dreadCavalrySpecialty` policy selects Tamika's replacement
specialty for Black Knights and Dread Knights. Its version is 1 and its named
`followupDamagePercent` coefficient is configurable. Absence retains historical
behavior; installed defaults never create the clause while loading an older hero.

A genuine enemy destruction by the creature's own ordinary physical melee action,
including its physical collateral, can grant one optional additional melee action
within the same Creature Activation. Native Rebirth is resolved before recognizing
a death. The first qualifying victim identity is latched; later resurrection does
not revoke the grant and does not make that identity a legal follow-up target.
Retaliation, reflection, spells and off-turn reactions cannot grant this action.

The follow-up grants no movement refill, Creature Activation, Hero Action, Mana,
new intrinsic Charge ability or extra Morale roll. Native melee legality applies,
including an adjacent attack with no remaining movement. Normal retaliation,
Luck, Death Blow and native multiple attacks remain under their existing owners.

`DreadCavalryState` belongs to the unit. Its used receipt persists through decline,
death and native Rebirth and resets only at a genuine activation. Crisis suspension
and resume preserve it. Pending state saves the original controller, victim,
remaining movement, output percentage and existing post-attack movement provenance.
The saved owner must be active or named by a validated Crisis return frame.

Pursuit and Opportunist share the original unused movement. Vanish supplies its
independent existing allowance. After spending movement on the charge, the retained
tail is the maximum of independent Vanish, the shared remainder and any genuinely
new native grant. These allowances are never added or refilled. Skipping the charge
leaves the separately earned ordinary movement tail available.

The accepted attack carries an ephemeral output snapshot through `AttackDescriptor`
and `BattleAttackInfo`, using the existing activation-output damage stage exactly
once. Preview pending state and accepted output are alternatives, not multiplicative
copies. Native retaliation and Brace do not inherit this snapshot. No resolving
phase is serialized and no new damage-script field or RNG stream is introduced.

The new serialized fields and continuation reason require their named save feature.
Older writers reject nonempty state before their enclosing packet or battle prefix;
older readers clear the absent state. Current JSON validates incoming receipts before
mutating the destination. Human input, AI projections and authoritative execution
share the same entitlement, target restrictions and grant transition helpers.
