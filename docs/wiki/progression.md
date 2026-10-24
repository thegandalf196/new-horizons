# Hero progression

[Wiki home](README.md) · [Progression](progression.md) · [Combat](combat.md) · [Orders](orders.md) · [Creatures](creatures.md) · [Heroes](heroes.md) · [Towns](towns.md) · [Skills](skills/README.md) · [Spells](spells/README.md)

## Hero attributes

In Heroes III Complete, a hero's Attack and Defense are added to the army's creature statistics. In New Horizons, creature Attack and Defense remain intrinsic combat values. Hero Attack and Defense contribute through Orders and effects that explicitly use them. A high-Attack hero therefore has a reason to issue an offensive Order, rather than receiving the same passive benefit on every attack.

Class attribute growth follows a fixed class pattern. The [class table](classes.md) lists all 18 starting attribute vectors and per-level gains; the [hero roster](hero-roster.md) lists every standard hero's starting Skills and perk. Spell Power and Knowledge remain important to magic, while Might and Magic heroes have different starting development and class-specific opportunities. Might heroes can develop **Command**, which strengthens Orders; Magic heroes can develop **Wisdom**, which improves spell efficiency.

## Leadership and stack capacity

Each army slot has capacity `floor(hero Leadership / creature requirement)`. A hero with 1,000 Leadership can command 20 creatures requiring 50 Leadership in **each** eligible slot. Filling one slot does not reduce the capacity of the other six.

Ordinary creature upgrades keep their base form's Leadership requirement. Upgrading a stack does not make that same stack exceed its capacity merely because its creatures became upgraded. Leadership requirements differ between creature families and are separate from recruitment Gold costs.

## Skills and perk tiers

A hero normally has room for **eight Skills**. Each Skill has Basic, Advanced and Expert ranks and **ten alternative perks: four Basic, four Advanced and two Expert**. You choose at most **one perk from each tier**, for three perks in a fully developed Skill. Choosing a perk permanently closes the other choices from that tier for that hero.

Ordinary progression alternates:

**Basic Skill → Basic perk → Advanced Skill → Advanced perk → Expert Skill → Expert perk.**

An external teacher or reward may advance a Skill rank when its rules permit it, but does not grant perks or skip their order. For example, a hero exceptionally taught Expert Offense without any Offense perks must still choose a Basic perk first, then Advanced, then Expert. Class, faction and other learning restrictions still apply.

A level-up offers **up to two Skill choices and two perk choices**. Choose **one** option overall. A Skill with an occupied perk tier cannot offer another perk until it reaches the next needed rank. An exhausted or otherwise illegal Skill does not occupy a perk slot merely to fill the screen.

### How perk offers are weighted

Ordinary Skill offers keep the full class preference weights. Perk offers soften those preferences: the most favored eligible parent Skill has at most **twice** the weight of the least favored one. The reference range comes from the class's complete positive Skill-weight table, not just the Skills currently eligible for perks. Learning another Skill or finishing one does not rescale the remaining Skills against each other.

The game first picks an eligible **parent Skill**, then picks one of that Skill's legal unlearned perks with equal chances. Having more available perks does not increase a Skill's chance to be picked. The second perk offer uses a different parent. With exactly two eligible parents, both appear when there are two offer slots; with only one eligible parent, only **one** perk is offered. No eligible parent means no perk offer.

A legitimately learned zero-weight Skill receives the minimum perk weight once it is eligible; this does not make a normally forbidden Skill available for ordinary acquisition. For a class whose positive weights range from 1 to 10, illustrative perk weights are:

| Ordinary Skill weight | Perk-parent weight |
| --- | --- |
| 1 | 1.000 |
| 2 | 1.232 |
| 5 | 1.623 |
| 10 | 2.000 |

These are relative weights, not fixed percentages of level-ups or guaranteed appearances.

### Skill-based attribute growth

Alongside normal class growth, these learned Skills each independently have a **10% / 20% / 30%** chance at Basic/Advanced/Expert to add one associated primary-attribute point at level-up:

| Skill | Additional attribute |
| --- | --- |
| Offense | +1 Attack |
| Archery | +1 Attack |
| Armorer | +1 Defense |
| Spellcraft | +1 Spell Power |
| Wisdom | +1 Knowledge |

Offense and Archery roll independently. Deep Knowledge adds 10 percentage points to Wisdom's chance. Hero Attack and Defense still do not become passive additions to every creature's Attack and Defense.

## Adventure movement

Adventure movement belongs to the hero and no longer falls simply because the army contains a slow creature. Terrain, roads and movement abilities still matter.

For the complete set of alternatives, browse the [Skill roster](skills/README.md). Exact creature capacities use the requirements described in [Creatures](creatures.md).
