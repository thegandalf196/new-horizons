# Nature — terrain, living troops and temporary allies

[Wiki home](../README.md) · [Progression](../progression.md) · [Combat](../combat.md) · [Orders](../orders.md) · [Creatures](../creatures.md) · [Heroes](../heroes.md) · [Towns](../towns.md) · [Skills](../skills/README.md) · [Spells](../spells/README.md)

Nature controls movement without treating every restraint as a lost turn. It also introduces healing that cannot restore casualties and summons that cannot become permanent recruits.

| Spell | Level; Mana | What it does |
| --- | --- | --- |
| **Entangle** | I; 4 | Roots one enemy ground stack for `min(2, 1 + floor(P / 100))` rounds. Voluntary movement becomes 0, but Initiative, adjacent attacks, retaliation, shooting, Wait, Defend and nonmovement abilities remain available. Teleportation or actual displacement breaks the roots. |
| **Regeneration** | I; 4 | For 3 rounds, marks `min(50%, 25% + 0.15% × P)` of damage suffered by surviving creatures as recoverable. Restores that marked HP at the start of the stack's next activation. Dead creatures are not restored. |
| **Vengeful Vines** | I; 5 | Select three distinct connected hexes; bends and triangles are legal. Each enemy stack touching the selected area takes `20 + 1.1 × P` damage once and loses **2 movement Speed for 2 rounds**. Initiative is unchanged; friendly stacks are unaffected. |
| **Poison** | II; 7 | Physically poisons one living enemy stack. Its next three activations suffer `20 + 0.5 × P`, then **1.5 times**, then **2 times** that damage. Poison does not spread. Cure removes it; ordinary Dispel does not. |
| **Summon Trolls** | II; 9 | Summons temporary Trolls with `100 + 2.5 × P` total HP at a legal empty hex. The final creature can begin wounded to preserve the exact HP pool. Trolls retain regeneration and disappear after combat. |
| **Verdant Prison** | III; 12 | Surrounds a chosen enemy with temporary Dendroid Guards on legal adjacent empty hexes. Divides one `180 + 3 × P` HP pool among the created stacks. Dendroids retain their normal abilities and Initiative and remain even if the target escapes. No legal placement means no cast. |
| **Earthquake** | III; 12 | Outside sieges, deals `30 + 0.8 × P` Nature damage in a radius-2 area and leaves fractured ground for 3 rounds, costing an extra movement point to cross. In sieges, instead deals **100 structural damage** to **2–4 eligible nearby fortification sections**, depending on spell strength. It no longer consists solely of random wall attacks as in Complete. |
| **Hydra's Vitality** | IV; 16 | For 3 rounds, increases each creature's maximum HP in one friendly living stack by `min(50%, 25% + 0.15% × P)`. Casting does not fill the new HP capacity. Each surviving creature regenerates 10% of its enhanced maximum HP at the start of its activation. Casualties stay dead; excess HP disappears when the effect expires. |
| **Quicksand** | IV; 16 / 13 | Places individually selected concealed patches on legal empty ground. An enemy cannot see a patch until triggered. A ground creature entering one stops and is trapped until its next activation; friendly troops can also trigger it. Flying creatures ignore it while airborne. In new games, the ordinary count is `min(5, 2 + floor(P / 60))` before School, Spellcraft and other applicable Spell Power bonuses. |
| **Elemental Convergence** | V; 22 | Summons temporary Elementals with `250 + 5 × P` total HP at a legal empty hex. The battlefield determines the kind: grass favors Air; snow, swamp, coasts and water favor Water; dirt, inland sand, rough ground and underground terrain favor Earth; lava favors Fire. Special magical battlefields can override the ordinary terrain choice. |
| **Nature's Wrath** | V; 23 | Select a first living stack. The spell chains to the nearest unvisited stack, up to **17 different stacks**. It damages enemies and heals surviving allies without resurrecting casualties. Initial strength is `110 + 2 × P`, retaining **93% per hop**. A protected recipient still consumes its place in the chain. |

School rank, Spellcraft, Warcasting and Empower scale Quicksand's Spell Power term before the patch-count floor; they do not multiply its two base patches.

**Quicksand in older campaigns:** before the new count rule, the patch threshold also included the caster's older Spell Power divisor. A hero using the earlier New Horizons attributes therefore needed 600 / 1,200 / 1,800 displayed Spell Power for three / four / five patches before bonuses. Those campaigns keep their recorded rule. New games use 60 / 120 / 180 instead. Mire Shaper's additional patch is applied after the ordinary five-patch cap.

**Nature perk examples:** Verdant Communion grants **Mass Regeneration** for **12 Mana**. Rootcaller extends Entangle by a round, to an ordinary maximum of 3. Herbalist adds ten percentage points to Regeneration before its 50% cap. Beastcaller improves the Troll HP pool by 25%; Verdant Warden improves Verdant Prison's pool by 25%; Elemental Conjurer improves its summon pool by 30% and grants +2 Initiative in the summon round. Mire Shaper adds one Quicksand patch beyond the ordinary cap. Worldroot adds two Nature's Wrath recipients and strengthens its Spell Power term.

[Spellcasting and notation](README.md#learning-and-casting) · [All schools](README.md) · [Magic Skills](../skills/magic.md)
