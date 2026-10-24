# Orders

[Wiki home](README.md) · [Progression](progression.md) · [Combat](combat.md) · [Orders](orders.md) · [Creatures](creatures.md) · [Heroes](heroes.md) · [Towns](towns.md) · [Skills](skills/README.md) · [Spells](spells/README.md)

A hero normally spends **one Hero Action per round**, choosing a Spell or an Order. Orders cost no Mana. Their effects normally end with the round; specific perks and faction abilities can alter their duration, strength or action allowance. War machines do not receive ordinary Order benefits.

The table gives the ordinary effects **before Command, perks and other special bonuses**. Here, **A** and **D** mean the hero's current Attack and Defense ratings, including modifiers. For orientation, a starting Knight has 30 Attack and 45 Defense. Round percentage results to the nearest whole number.

| Order | What it does | Ordinary strength |
| --- | --- | --- |
| **Charge!** | Each eligible friendly stack gains a bonus on its first qualifying melee attack after moving at least three hexes. | +`10 + 0.20 × A`% melee damage. |
| **Hold the Line!** | Protects eligible friendly stacks while they remain at the positions they occupied when the Order was issued. | `10 + 0.20 × D`% less physical damage. |
| **Focus Fire!** | Marks one enemy. Eligible friendly shooters deal more ranged damage against that target and halve ordinary distance and obstacle damage penalties, from 50% to 25%. Legal adjacent shots also receive the reduced penalty; targeting restrictions still apply. | +`5 + 0.15 × A`% ranged damage. |
| **Riposte!** | Reduces incoming melee damage and strengthens retaliations. | `5 + 0.10 × D`% less melee damage; +`5 + 0.15 × A`% retaliation damage. |
| **Brace!** | Gives eligible defenders a pre-emptive strike against an enemy that moves at least three hexes into a melee attack. | Pre-emptive strike deals `50 + 0.25 × D`% of its normal damage. |
| **Protect!** | Choose a Protector, then an adjacent friendly Ward. The Protector intercepts the next qualifying melee attack against the Ward while they remain adjacent. | Intercepted attack deals `5 + 0.15 × D`% less physical damage to the Protector. Normally one interception. |
| **Flank!** | Marks one enemy for stronger melee attacks. Attacking it from additional distinct sides increases the bonus further. | +`8 + 0.12 × A`% melee damage, plus 4 percentage points for each additional side used. |
| **Second Wind!** | Gives a friendly stack that has completed its activation another activation this round, with reduced damage. | Retains `50 + round(0.015 × Leadership)`% damage, capped at 100%. |

**Second Wind's percentage is damage, not the chance of receiving the activation.** With 1,000 Leadership and no further bonuses, that additional activation deals 65% damage.

Command increases the attribute-derived part of Order strength by **10% / 20% / 30%** at Basic / Advanced / Expert. It does not multiply the flat starting value. For example, Charge! with 40 Attack gives +18% damage without Command, or +20% with Expert Command after rounding. Perks can specialize this further, and Warcasting rewards alternating Orders and Spells.

The [combat rules](combat.md) explain the shared Hero Action and damage-reduction cap. The [Martial Skills](skills/martial.md) develop individual Orders, while [Warcasting](skills/magic.md#warcasting) rewards alternating them with spells.
