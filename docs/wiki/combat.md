# Combat rules

[Wiki home](README.md) · [Progression](progression.md) · [Combat](combat.md) · [Orders](orders.md) · [Creatures](creatures.md) · [Heroes](heroes.md) · [Towns](towns.md) · [Skills](skills/README.md) · [Spells](spells/README.md)

## Hero actions and creature activations

A hero normally has **one Hero Action each combat round**, spent on either a Spell or an Order. Orders cost no Mana. Particular perks and faction abilities can grant additional opportunities with their own restrictions. See the [Order reference](orders.md), [spellcasting rules](spells/README.md#learning-and-casting) and [faction Skills](skills/faction.md).

## Speed and Initiative

**Speed** determines battlefield movement. **Initiative** determines activation order. Creatures with a separately defined Initiative use it independently of Speed. Some creatures retain a legacy fallback to their effective Speed; Speed effects can also change their activation order. The [creature roster](creature-roster.md) marks these entries with †. Movement-only bonuses do not change Initiative. Adventure movement belongs to the hero; its progression rules are described under [Hero progression](progression.md#adventure-movement).

## Damage reduction

Ordinary physical damage reductions combine multiplicatively, with an overall 80% reduction cap. Two 20% reductions therefore reduce damage by 36%, rather than 40%. Effects explicitly described as final damage modifiers are applied separately.

## Frozen and Shatter

- Each successful Ice Elemental melee attack **or retaliation** has a **20% chance** to Freeze its surviving target. Roll once for the stack's resolved attack, not once per creature. Normal damage happens first; Freeze then applies before the defender could retaliate.
- A Frozen stack cannot move, attack, retaliate, Wait, Defend or use creature abilities. It forfeits its next **normal activation**, then thaws. Extra activations do not thaw it.
- The first subsequent ordinary physical creature attack receives **+25% final damage** and Shatters the ice after dealing damage. The victim cannot retaliate against that hit. War machines and indirect damage do not receive the Shatter bonus.
- Direct magical damage also breaks the ice after dealing damage, with **no Shatter bonus**.
- **Cure can remove Frozen; ordinary Dispel cannot.** Freeing a stack before its normal activation lets it act normally when that activation arrives. Freeing it afterward does not recover a forfeited activation.
- Freeze cannot stack or refresh, and the same target can become Frozen **at most once per combat round**, even if it was freed in between.

The choice is tactical: leave the target Frozen to deny an activation, or Shatter it for extra damage while freeing it to act later.

## Terms used in this wiki

A **Hero Action** is an opportunity to cast a Spell or issue an Order. A **creature activation** is one stack's opportunity to act; gaining one does not give the hero another Spell or Order. An Order-only opportunity cannot be spent on a Spell.

**Total HP** or **aggregate HP** means the entire stack's health rather than one creature. Current HP is what remains alive; maximum HP is the stack's full reference health. Healing survivors does not restore casualties unless the effect explicitly says it can.

**Army Value** is the game's relative army-strength rating, not creature headcount. A **percentage point** is added directly to a percentage: +10 points changes 20% to 30%, rather than to 22%.
