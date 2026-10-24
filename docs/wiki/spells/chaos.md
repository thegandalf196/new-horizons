# Chaos — unreliable control and dangerous bargains

[Wiki home](../README.md) · [Progression](../progression.md) · [Combat](../combat.md) · [Orders](../orders.md) · [Creatures](../creatures.md) · [Heroes](../heroes.md) · [Towns](../towns.md) · [Skills](../skills/README.md) · [Spells](../spells/README.md)

Chaos manipulates intentions, positions, bodies and the distribution of magical effects. Several spells can help or harm either army, making target selection more important than the size of the damage number.

| Spell | Level; Mana | What it does |
| --- | --- | --- |
| **Blink** | I; 4 | Teleports one friendly or enemy stack to a random legal position within `min(4, 2 + floor(P / 100))` hexes. Ignores intervening barriers, but the full footprint must fit. It grants no extra action, retaliation or Initiative. No legal destination means no Mana or action spent. |
| **Confusion** | I; 5 | Replaces the target's next activation with a random **Attack, Defend or Wander** behavior. An impossible attack can fall back to approach or Defend. Applying it removes Berserk; a negative-Morale forfeiture consumes the pending Confusion, and Dispel can remove it before it resolves. |
| **Misfortune** | I; 4 / 3 | Prevents positive Luck on one enemy without changing negative Luck. Favorable random creature abilities use `max(25%, 75% − 0.25% × P)` of their normal chance; deterministic abilities remain deterministic. Lasts `min(4, 2 + floor(P / 80))` rounds. |
| **Berserk** | II; 8 / 6 | One enemy attacks the nearest creature regardless of allegiance, judging distance by movement cost. Shooters are forced into melee; a distant target is approached. The compulsion lasts until its next attack. Higher school ranks do not turn it into the old larger area spell. |
| **Forgetfulness** | II; 8 | For `min(3, 1 + floor(P / 80))` rounds, one enemy may only move, make basic melee attacks, Wait and Defend. Shooting, activated abilities, special attacks, triggered abilities and creature spellcasting are suppressed. Ordinary retaliation and basic creature properties remain. It is not merely reduced shooting damage. |
| **Hand of Fate** | III; 12 | Deals `70 + 2.5 × P` to one enemy, then randomly chooses another surviving battlefield stack, **friend or foe**, for damage based on **half the first target's actual HP loss**. The second target applies its own magical defenses; a blocked result is not rerolled. |
| **Polymorph** | III; 12 | Turns one enemy into a random creature of the same category for 2 rounds. Preserves aggregate current HP, allegiance and Initiative position, relocating only if the new footprint requires it. Expiry or Dispel restores the original body with surviving HP; restoration can wait if no legal space exists. |
| **Puppet Master** | IV; 16 | Controls one enemy for **one creature activation**, without changing its allegiance. Wait postpones that same controlled activation rather than creating another. Afterward, the stack gains **2 rounds of Lucidity**, preventing further mental domination/control. |
| **Reality Warp** | IV; 15 | Exchanges transferable temporary magical buffs and debuffs between one friendly and one enemy stack, preserving their strength and remaining duration. Illegal effects stay with their original recipient. Orders, equipment, intrinsic traits, terrain, summoned identity and transformations do not move. If a hostile target resists, neither side exchanges effects. |
| **Shield of Chaos** | V; 23 | For 2 rounds, gives one friendly or enemy stack physical and magical damage reduction of `min(80%, 50% + 0.15% × P)`, but also **−10 Morale and −10 Luck**. It does not stop non-damaging magic. |
| **Pandemonium** | V; 22 | Hits every living battlefield stack, including allies. Each takes damage equal to **its number of distinct active debuffs × `(20 + 0.25 × P)`**. Stacks without debuffs take no damage. Debuffs are counted before any damage resolves. |

**Chaos perk examples:** Blinkmaster considers two legal Blink destinations and chooses the farther. Mindbreaker adds suppression of passive offensive abilities to Forgetfulness. Reality Breaker permits Reality Warp between any two otherwise legal stacks. Paradox Shield adds ten percentage points to Shield of Chaos: physical reduction remains capped at 80%, while that spell's magical protection can reach 90%. Pandemonium Master increases each debuff's contribution by 25%.

[Spellcasting and notation](README.md#learning-and-casting) · [All schools](README.md) · [Magic Skills](../skills/magic.md)
