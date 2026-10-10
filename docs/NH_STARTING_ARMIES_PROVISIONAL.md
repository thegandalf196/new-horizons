# Provisional starting-army dispositions — all144

Focused validation: the current Linux build passes all nine actual hero tests:
all144 starting-development packages, all144 default army compositions and
Leadership-safe sampled ranges, explicit map-army preservation, world save/load,
historical capability absence, and four strict profile/prototype perk-overlap
controls. Two exact data checks and module-generation verification also pass.
These are implementation checks, not numerical balance or playable-delivery
acceptance. The ordinary launcher has not been promoted by this change.

Chosen rule: preserve original prototype composition, min/max sampling and ordinary 100/88/25 correlated slot-inclusion chances. Clamp each included troop slot to its current level1 Leadership maximum. This is EXISTING production behavior, not a new captured table or epoch. Ranges below describe min(original sampled value, maximum), not newly flattened authoring ranges.
Cabir uses the retained core:gremlin ID. Original Pixie/Sprite remain alongside separate Wisp; no starting Pixie→Wisp substitution. Original war machines retain artifact/inclusion/equipment behavior.
Bron's explicit config/heroes/fortress.json override takes precedence over the differing historical workbook reference. Native checks use loaded original prototypes, not the reference as data authority.
The existing shared resolver retains upgraded120%/nearest10 cost handling; no current workbook original row asks for an upgraded species. Map-prescribed and saved/reinitialized armies are unchanged. Values below are class baselines without added artifacts; actual shared capacity includes applicable current bonuses.
Every ordinary first slot has a positive minimum and positive capacity, so normal initialization retains at least one legal troop stack. Numerical tuning is deferred. Cap-saturated effective ranges are provisional consequences of the existing Leadership numbers, not new balance proposals.

| Hero | Class Leadership | Original included slots | Leadership-safe included ranges |
| --- | --- | --- | --- |
| core:orrin | core:knight: 1025 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–17 (cap 17) · core:archer 4–7 (cap 11) · core:griffin 2–3 (cap 4) |
| core:valeska | core:knight: 1025 | core:archer 4–7 · core:archer 4–7 · core:archer 4–7 | core:archer 4–7 (cap 11) · core:archer 4–7 (cap 11) · core:archer 4–7 (cap 11) |
| core:edric | core:knight: 1025 | core:pikeman 10–20 · core:griffin 2–3 · core:griffin 2–3 | core:pikeman 10–17 (cap 17) · core:griffin 2–3 (cap 4) · core:griffin 2–3 (cap 4) |
| core:sylvia | core:knight: 1025 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–17 (cap 17) · core:archer 4–7 (cap 11) · core:griffin 2–3 (cap 4) |
| core:lordHaart | core:knight: 1025 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–17 (cap 17) · core:archer 4–7 (cap 11) · core:griffin 2–3 (cap 4) |
| core:sorsha | core:knight: 1025 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–17 (cap 17) · core:archer 4–7 (cap 11) · core:griffin 2–3 (cap 4) |
| core:christian | core:knight: 1025 | core:pikeman 10–20 · core:griffin 2–3 · Ballista | core:pikeman 10–17 (cap 17) · core:griffin 2–3 (cap 4) · Ballista |
| core:tyris | core:knight: 1025 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–17 (cap 17) · core:archer 4–7 (cap 11) · core:griffin 2–3 (cap 4) |
| core:adela | core:cleric: 725 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–12 (cap 12) · core:archer 4–7 (cap 8) · core:griffin 2–3 (cap 3) |
| core:cuthbert | core:cleric: 725 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–12 (cap 12) · core:archer 4–7 (cap 8) · core:griffin 2–3 (cap 3) |
| core:adelaide | core:cleric: 725 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–12 (cap 12) · core:archer 4–7 (cap 8) · core:griffin 2–3 (cap 3) |
| core:ingham | core:cleric: 725 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–12 (cap 12) · core:archer 4–7 (cap 8) · core:griffin 2–3 (cap 3) |
| core:sanya | core:cleric: 725 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–12 (cap 12) · core:archer 4–7 (cap 8) · core:griffin 2–3 (cap 3) |
| core:loynis | core:cleric: 725 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–12 (cap 12) · core:archer 4–7 (cap 8) · core:griffin 2–3 (cap 3) |
| core:caitlin | core:cleric: 725 | core:pikeman 10–20 · core:archer 4–7 · core:griffin 2–3 | core:pikeman 10–12 (cap 12) · core:archer 4–7 (cap 8) · core:griffin 2–3 (cap 3) |
| core:rion | core:cleric: 725 | core:pikeman 10–20 · core:griffin 2–3 · First Aid Tent | core:pikeman 10–12 (cap 12) · core:griffin 2–3 (cap 3) · First Aid Tent |
| core:mephala | core:ranger: 950 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 12–15 (cap 15) · core:dwarf 3–5 (cap 10) · core:woodElf 3–6 (cap 6) |
| core:ufretin | core:ranger: 950 | core:dwarf 3–5 · core:dwarf 3–5 · core:dwarf 3–5 | core:dwarf 3–5 (cap 10) · core:dwarf 3–5 (cap 10) · core:dwarf 3–5 (cap 10) |
| core:jenova | core:ranger: 950 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 12–15 (cap 15) · core:dwarf 3–5 (cap 10) · core:woodElf 3–6 (cap 6) |
| core:ryland | core:ranger: 950 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 12–15 (cap 15) · core:dwarf 3–5 (cap 10) · core:woodElf 3–6 (cap 6) |
| core:thorgrim | core:ranger: 950 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 12–15 (cap 15) · core:dwarf 3–5 (cap 10) · core:woodElf 3–6 (cap 6) |
| core:ivor | core:ranger: 950 | core:centaur 12–24 · core:woodElf 3–6 · core:woodElf 3–6 | core:centaur 12–15 (cap 15) · core:woodElf 3–6 (cap 6) · core:woodElf 3–6 (cap 6) |
| core:clancy | core:ranger: 950 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 12–15 (cap 15) · core:dwarf 3–5 (cap 10) · core:woodElf 3–6 (cap 6) |
| core:kyrre | core:ranger: 950 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 12–15 (cap 15) · core:dwarf 3–5 (cap 10) · core:woodElf 3–6 (cap 6) |
| core:coronius | core:druid: 650 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 10–10 (cap 10) · core:dwarf 3–5 (cap 7) · core:woodElf 3–4 (cap 4) |
| core:uland | core:druid: 650 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 10–10 (cap 10) · core:dwarf 3–5 (cap 7) · core:woodElf 3–4 (cap 4) |
| core:elleshar | core:druid: 650 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 10–10 (cap 10) · core:dwarf 3–5 (cap 7) · core:woodElf 3–4 (cap 4) |
| core:gem | core:druid: 650 | core:centaur 12–24 · core:woodElf 3–6 · First Aid Tent | core:centaur 10–10 (cap 10) · core:woodElf 3–4 (cap 4) · First Aid Tent |
| core:malcom | core:druid: 650 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 10–10 (cap 10) · core:dwarf 3–5 (cap 7) · core:woodElf 3–4 (cap 4) |
| core:melodia | core:druid: 650 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 10–10 (cap 10) · core:dwarf 3–5 (cap 7) · core:woodElf 3–4 (cap 4) |
| core:alagar | core:druid: 650 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 10–10 (cap 10) · core:dwarf 3–5 (cap 7) · core:woodElf 3–4 (cap 4) |
| core:aeris | core:druid: 650 | core:centaur 12–24 · core:dwarf 3–5 · core:woodElf 3–6 | core:centaur 10–10 (cap 10) · core:dwarf 3–5 (cap 7) · core:woodElf 3–4 (cap 4) |
| core:piquedram | core:alchemist: 875 | core:stoneGargoyle 3–5 · core:stoneGargoyle 3–5 · core:stoneGargoyle 3–5 | core:stoneGargoyle 3–5 (cap 10) · core:stoneGargoyle 3–5 (cap 10) · core:stoneGargoyle 3–5 (cap 10) |
| core:josephine | core:alchemist: 875 | core:gremlin 30–40 · core:ironGolem 2–3 · core:ironGolem 2–3 | core:gremlin 17–17 (cap 17) · core:ironGolem 2–3 (cap 6) · core:ironGolem 2–3 (cap 6) |
| core:neela | core:alchemist: 875 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 17–17 (cap 17) · core:stoneGargoyle 3–5 (cap 10) · core:ironGolem 2–3 (cap 6) |
| core:torosar | core:alchemist: 875 | core:gremlin 30–40 · core:ironGolem 2–3 · Ballista | core:gremlin 17–17 (cap 17) · core:ironGolem 2–3 (cap 6) · Ballista |
| core:fafner | core:alchemist: 875 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 17–17 (cap 17) · core:stoneGargoyle 3–5 (cap 10) · core:ironGolem 2–3 (cap 6) |
| core:halon | core:wizard: 650 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 13–13 (cap 13) · core:stoneGargoyle 3–5 (cap 8) · core:ironGolem 2–3 (cap 4) |
| core:iona | core:alchemist: 875 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 17–17 (cap 17) · core:stoneGargoyle 3–5 (cap 10) · core:ironGolem 2–3 (cap 6) |
| core:rissa | core:alchemist: 875 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 17–17 (cap 17) · core:stoneGargoyle 3–5 (cap 10) · core:ironGolem 2–3 (cap 6) |
| core:astral | core:wizard: 650 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 13–13 (cap 13) · core:stoneGargoyle 3–5 (cap 8) · core:ironGolem 2–3 (cap 4) |
| core:serena | core:wizard: 650 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 13–13 (cap 13) · core:stoneGargoyle 3–5 (cap 8) · core:ironGolem 2–3 (cap 4) |
| core:daremyth | core:wizard: 650 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 13–13 (cap 13) · core:stoneGargoyle 3–5 (cap 8) · core:ironGolem 2–3 (cap 4) |
| core:theodorus | core:wizard: 650 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 13–13 (cap 13) · core:stoneGargoyle 3–5 (cap 8) · core:ironGolem 2–3 (cap 4) |
| core:solmyr | core:wizard: 650 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 13–13 (cap 13) · core:stoneGargoyle 3–5 (cap 8) · core:ironGolem 2–3 (cap 4) |
| core:cyra | core:wizard: 650 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 13–13 (cap 13) · core:stoneGargoyle 3–5 (cap 8) · core:ironGolem 2–3 (cap 4) |
| core:aine | core:wizard: 650 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 13–13 (cap 13) · core:stoneGargoyle 3–5 (cap 8) · core:ironGolem 2–3 (cap 4) |
| core:thane | core:alchemist: 875 | core:gremlin 30–40 · core:stoneGargoyle 3–5 · core:ironGolem 2–3 | core:gremlin 17–17 (cap 17) · core:stoneGargoyle 3–5 (cap 10) · core:ironGolem 2–3 (cap 6) |
| core:fiona | core:demoniac: 1025 | core:imp 15–25 · core:hellHound 3–4 · core:hellHound 3–4 | core:imp 15–20 (cap 20) · core:hellHound 3–4 (cap 6) · core:hellHound 3–4 (cap 6) |
| core:rashka | core:demoniac: 1025 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 15–20 (cap 20) · core:gog 4–7 (cap 11) · core:hellHound 3–4 (cap 6) |
| core:marius | core:demoniac: 1025 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 15–20 (cap 20) · core:gog 4–7 (cap 11) · core:hellHound 3–4 (cap 6) |
| core:ignatius | core:demoniac: 1025 | core:imp 15–25 · core:imp 15–25 · core:imp 15–25 | core:imp 15–20 (cap 20) · core:imp 15–20 (cap 20) · core:imp 15–20 (cap 20) |
| core:octavia | core:demoniac: 1025 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 15–20 (cap 20) · core:gog 4–7 (cap 11) · core:hellHound 3–4 (cap 6) |
| core:calh | core:demoniac: 1025 | core:gog 4–7 · core:gog 4–7 · core:gog 4–7 | core:gog 4–7 (cap 11) · core:gog 4–7 (cap 11) · core:gog 4–7 (cap 11) |
| core:pyre | core:demoniac: 1025 | core:imp 15–25 · core:hellHound 3–4 · Ballista | core:imp 15–20 (cap 20) · core:hellHound 3–4 (cap 6) · Ballista |
| core:nymus | core:demoniac: 1025 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 15–20 (cap 20) · core:gog 4–7 (cap 11) · core:hellHound 3–4 (cap 6) |
| core:ayden | core:heretic: 725 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 14–14 (cap 14) · core:gog 4–7 (cap 8) · core:hellHound 3–4 (cap 4) |
| core:xyron | core:heretic: 725 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 14–14 (cap 14) · core:gog 4–7 (cap 8) · core:hellHound 3–4 (cap 4) |
| core:axsis | core:heretic: 725 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 14–14 (cap 14) · core:gog 4–7 (cap 8) · core:hellHound 3–4 (cap 4) |
| core:olema | core:heretic: 725 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 14–14 (cap 14) · core:gog 4–7 (cap 8) · core:hellHound 3–4 (cap 4) |
| core:calid | core:heretic: 725 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 14–14 (cap 14) · core:gog 4–7 (cap 8) · core:hellHound 3–4 (cap 4) |
| core:ash | core:heretic: 725 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 14–14 (cap 14) · core:gog 4–7 (cap 8) · core:hellHound 3–4 (cap 4) |
| core:xarfax | core:heretic: 725 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 14–14 (cap 14) · core:gog 4–7 (cap 8) · core:hellHound 3–4 (cap 4) |
| core:zydar | core:heretic: 725 | core:imp 15–25 · core:gog 4–7 · core:hellHound 3–4 | core:imp 14–14 (cap 14) · core:gog 4–7 (cap 8) · core:hellHound 3–4 (cap 4) |
| core:straker | core:deathknight: 950 | core:walkingDead 4–6 · core:walkingDead 4–6 · core:walkingDead 4–6 | core:walkingDead 4–6 (cap 11) · core:walkingDead 4–6 (cap 11) · core:walkingDead 4–6 (cap 11) |
| core:vokial | core:deathknight: 950 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 20–21 (cap 21) · core:walkingDead 4–6 (cap 11) · core:wight 4–6 (cap 7) |
| core:moandor | core:deathknight: 950 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 20–21 (cap 21) · core:walkingDead 4–6 (cap 11) · core:wight 4–6 (cap 7) |
| core:charna | core:deathknight: 950 | core:skeleton 20–30 · core:wight 4–6 · core:wight 4–6 | core:skeleton 20–21 (cap 21) · core:wight 4–6 (cap 7) · core:wight 4–6 (cap 7) |
| core:tamika | core:deathknight: 950 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 20–21 (cap 21) · core:walkingDead 4–6 (cap 11) · core:wight 4–6 (cap 7) |
| core:isra | core:deathknight: 950 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 20–21 (cap 21) · core:walkingDead 4–6 (cap 11) · core:wight 4–6 (cap 7) |
| core:clavius | core:deathknight: 950 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 20–21 (cap 21) · core:walkingDead 4–6 (cap 11) · core:wight 4–6 (cap 7) |
| core:galthran | core:deathknight: 950 | core:skeleton 20–30 · core:skeleton 20–30 · core:skeleton 20–30 | core:skeleton 20–21 (cap 21) · core:skeleton 20–21 (cap 21) · core:skeleton 20–21 (cap 21) |
| core:septienna | core:necromancer: 725 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 16–16 (cap 16) · core:walkingDead 4–6 (cap 9) · core:wight 4–5 (cap 5) |
| core:aislinn | core:necromancer: 725 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 16–16 (cap 16) · core:walkingDead 4–6 (cap 9) · core:wight 4–5 (cap 5) |
| core:sandro | core:necromancer: 725 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 16–16 (cap 16) · core:walkingDead 4–6 (cap 9) · core:wight 4–5 (cap 5) |
| core:nimbus | core:necromancer: 725 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 16–16 (cap 16) · core:walkingDead 4–6 (cap 9) · core:wight 4–5 (cap 5) |
| core:thant | core:necromancer: 725 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 16–16 (cap 16) · core:walkingDead 4–6 (cap 9) · core:wight 4–5 (cap 5) |
| core:xsi | core:necromancer: 725 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 16–16 (cap 16) · core:walkingDead 4–6 (cap 9) · core:wight 4–5 (cap 5) |
| core:vidomina | core:necromancer: 725 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 16–16 (cap 16) · core:walkingDead 4–6 (cap 9) · core:wight 4–5 (cap 5) |
| core:nagash | core:necromancer: 725 | core:skeleton 20–30 · core:walkingDead 4–6 · core:wight 4–6 | core:skeleton 16–16 (cap 16) · core:walkingDead 4–6 (cap 9) · core:wight 4–5 (cap 5) |
| core:lorelei | core:overlord: 1025 | core:harpy 4–6 · core:harpy 4–6 · core:harpy 4–6 | core:harpy 4–6 (cap 11) · core:harpy 4–6 (cap 11) · core:harpy 4–6 (cap 11) |
| core:arlach | core:overlord: 1025 | core:troglodyte 30–40 · core:beholder 3–4 · Ballista | core:troglodyte 18–18 (cap 18) · core:beholder 3–4 (cap 7) · Ballista |
| core:dace | core:overlord: 1025 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 18–18 (cap 18) · core:harpy 4–6 (cap 11) · core:beholder 3–4 (cap 7) |
| core:ajit | core:overlord: 1025 | core:troglodyte 30–40 · core:beholder 3–4 · core:beholder 3–4 | core:troglodyte 18–18 (cap 18) · core:beholder 3–4 (cap 7) · core:beholder 3–4 (cap 7) |
| core:damacon | core:overlord: 1025 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 18–18 (cap 18) · core:harpy 4–6 (cap 11) · core:beholder 3–4 (cap 7) |
| core:gunnar | core:overlord: 1025 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 18–18 (cap 18) · core:harpy 4–6 (cap 11) · core:beholder 3–4 (cap 7) |
| core:synca | core:overlord: 1025 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 18–18 (cap 18) · core:harpy 4–6 (cap 11) · core:beholder 3–4 (cap 7) |
| core:shakti | core:overlord: 1025 | core:troglodyte 30–40 · core:troglodyte 30–40 · core:troglodyte 30–40 | core:troglodyte 18–18 (cap 18) · core:troglodyte 18–18 (cap 18) · core:troglodyte 18–18 (cap 18) |
| core:alamar | core:warlock: 725 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 13–13 (cap 13) · core:harpy 4–6 (cap 8) · core:beholder 3–4 (cap 5) |
| core:jaegar | core:warlock: 725 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 13–13 (cap 13) · core:harpy 4–6 (cap 8) · core:beholder 3–4 (cap 5) |
| core:malekith | core:warlock: 725 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 13–13 (cap 13) · core:harpy 4–6 (cap 8) · core:beholder 3–4 (cap 5) |
| core:jeddite | core:warlock: 725 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 13–13 (cap 13) · core:harpy 4–6 (cap 8) · core:beholder 3–4 (cap 5) |
| core:geon | core:warlock: 725 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 13–13 (cap 13) · core:harpy 4–6 (cap 8) · core:beholder 3–4 (cap 5) |
| core:deemer | core:warlock: 725 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 13–13 (cap 13) · core:harpy 4–6 (cap 8) · core:beholder 3–4 (cap 5) |
| core:sephinroth | core:warlock: 725 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 13–13 (cap 13) · core:harpy 4–6 (cap 8) · core:beholder 3–4 (cap 5) |
| core:darkstorn | core:warlock: 725 | core:troglodyte 30–40 · core:harpy 4–6 · core:beholder 3–4 | core:troglodyte 13–13 (cap 13) · core:harpy 4–6 (cap 8) · core:beholder 3–4 (cap 5) |
| core:yog | core:barbarian: 1100 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–22 (cap 22) · core:goblinWolfRider 5–7 (cap 12) · core:orc 4–6 (cap 7) |
| core:gurnisson | core:barbarian: 1100 | core:goblin 15–25 · core:orc 4–6 · Ballista | core:goblin 15–22 (cap 22) · core:orc 4–6 (cap 7) · Ballista |
| core:jabarkas | core:barbarian: 1100 | core:goblin 15–25 · core:orc 4–6 · core:orc 4–6 | core:goblin 15–22 (cap 22) · core:orc 4–6 (cap 7) · core:orc 4–6 (cap 7) |
| core:shiva | core:barbarian: 1100 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–22 (cap 22) · core:goblinWolfRider 5–7 (cap 12) · core:orc 4–6 (cap 7) |
| core:gretchin | core:barbarian: 1100 | core:goblin 15–25 · core:goblin 15–25 · core:goblin 15–25 | core:goblin 15–22 (cap 22) · core:goblin 15–22 (cap 22) · core:goblin 15–22 (cap 22) |
| core:krellion | core:barbarian: 1100 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–22 (cap 22) · core:goblinWolfRider 5–7 (cap 12) · core:orc 4–6 (cap 7) |
| core:cragHack | core:barbarian: 1100 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–22 (cap 22) · core:goblinWolfRider 5–7 (cap 12) · core:orc 4–6 (cap 7) |
| core:tyraxor | core:barbarian: 1100 | core:goblinWolfRider 5–7 · core:goblinWolfRider 5–7 · core:goblinWolfRider 5–7 | core:goblinWolfRider 5–7 (cap 12) · core:goblinWolfRider 5–7 (cap 12) · core:goblinWolfRider 5–7 (cap 12) |
| core:gird | core:battlemage: 875 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–17 (cap 17) · core:goblinWolfRider 5–7 (cap 9) · core:orc 4–6 (cap 6) |
| core:vey | core:battlemage: 875 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–17 (cap 17) · core:goblinWolfRider 5–7 (cap 9) · core:orc 4–6 (cap 6) |
| core:dessa | core:battlemage: 875 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–17 (cap 17) · core:goblinWolfRider 5–7 (cap 9) · core:orc 4–6 (cap 6) |
| core:terek | core:battlemage: 875 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–17 (cap 17) · core:goblinWolfRider 5–7 (cap 9) · core:orc 4–6 (cap 6) |
| core:zubin | core:battlemage: 875 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–17 (cap 17) · core:goblinWolfRider 5–7 (cap 9) · core:orc 4–6 (cap 6) |
| core:gundula | core:battlemage: 875 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–17 (cap 17) · core:goblinWolfRider 5–7 (cap 9) · core:orc 4–6 (cap 6) |
| core:oris | core:battlemage: 875 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–17 (cap 17) · core:goblinWolfRider 5–7 (cap 9) · core:orc 4–6 (cap 6) |
| core:saurug | core:battlemage: 875 | core:goblin 15–25 · core:goblinWolfRider 5–7 · core:orc 4–6 | core:goblin 15–17 (cap 17) · core:goblinWolfRider 5–7 (cap 9) · core:orc 4–6 (cap 6) |
| core:bron | core:beastmaster: 1100 | core:gnoll 10–20 · core:basilisk 4–7 · core:serpentFly 2–4 | core:gnoll 10–20 (cap 20) · core:basilisk 4–4 (cap 4) · core:serpentFly 2–4 (cap 7) |
| core:drakon | core:beastmaster: 1100 | core:gnoll 10–20 · core:gnoll 10–20 · core:gnoll 10–20 | core:gnoll 10–20 (cap 20) · core:gnoll 10–20 (cap 20) · core:gnoll 10–20 (cap 20) |
| core:wystan | core:beastmaster: 1100 | core:lizardman 4–7 · core:lizardman 4–7 · core:lizardman 4–7 | core:lizardman 4–7 (cap 12) · core:lizardman 4–7 (cap 12) · core:lizardman 4–7 (cap 12) |
| core:tazar | core:beastmaster: 1100 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–20 (cap 20) · core:lizardman 4–7 (cap 12) · core:serpentFly 2–4 (cap 7) |
| core:alkin | core:beastmaster: 1100 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–20 (cap 20) · core:lizardman 4–7 (cap 12) · core:serpentFly 2–4 (cap 7) |
| core:korbac | core:beastmaster: 1100 | core:gnoll 10–20 · core:serpentFly 2–4 · core:serpentFly 2–4 | core:gnoll 10–20 (cap 20) · core:serpentFly 2–4 (cap 7) · core:serpentFly 2–4 (cap 7) |
| core:gerwulf | core:beastmaster: 1100 | core:gnoll 10–20 · core:serpentFly 2–4 · Ballista | core:gnoll 10–20 (cap 20) · core:serpentFly 2–4 (cap 7) · Ballista |
| core:broghild | core:beastmaster: 1100 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–20 (cap 20) · core:lizardman 4–7 (cap 12) · core:serpentFly 2–4 (cap 7) |
| core:mirlanda | core:witch: 725 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–13 (cap 13) · core:lizardman 4–7 (cap 8) · core:serpentFly 2–4 (cap 5) |
| core:rosic | core:witch: 725 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–13 (cap 13) · core:lizardman 4–7 (cap 8) · core:serpentFly 2–4 (cap 5) |
| core:voy | core:witch: 725 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–13 (cap 13) · core:lizardman 4–7 (cap 8) · core:serpentFly 2–4 (cap 5) |
| core:verdish | core:witch: 725 | core:gnoll 10–20 · core:serpentFly 2–4 · First Aid Tent | core:gnoll 10–13 (cap 13) · core:serpentFly 2–4 (cap 5) · First Aid Tent |
| core:merist | core:witch: 725 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–13 (cap 13) · core:lizardman 4–7 (cap 8) · core:serpentFly 2–4 (cap 5) |
| core:styg | core:witch: 725 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–13 (cap 13) · core:lizardman 4–7 (cap 8) · core:serpentFly 2–4 (cap 5) |
| core:andra | core:witch: 725 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–13 (cap 13) · core:lizardman 4–7 (cap 8) · core:serpentFly 2–4 (cap 5) |
| core:tiva | core:witch: 725 | core:gnoll 10–20 · core:lizardman 4–7 · core:serpentFly 2–4 | core:gnoll 10–13 (cap 13) · core:lizardman 4–7 (cap 8) · core:serpentFly 2–4 (cap 5) |
| core:pasis | core:planeswalker: 875 | core:pixie 15–25 · core:airElemental 3–5 · core:airElemental 2–3 | core:pixie 15–19 (cap 19) · core:airElemental 3–3 (cap 3) · core:airElemental 2–3 (cap 3) |
| core:thunar | core:planeswalker: 875 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 15–19 (cap 19) · core:airElemental 3–3 (cap 3) · core:waterElemental 2–3 (cap 3) |
| core:ignissa | core:planeswalker: 875 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 15–19 (cap 19) · core:airElemental 3–3 (cap 3) · core:waterElemental 2–3 (cap 3) |
| core:lacus | core:planeswalker: 875 | core:pixie 15–25 · core:waterElemental 3–5 · core:waterElemental 1–2 | core:pixie 15–19 (cap 19) · core:waterElemental 3–3 (cap 3) · core:waterElemental 1–2 (cap 3) |
| core:monere | core:planeswalker: 875 | core:pixie 15–25 · core:airElemental 3–5 · core:airElemental 2–3 | core:pixie 15–19 (cap 19) · core:airElemental 3–3 (cap 3) · core:airElemental 2–3 (cap 3) |
| core:erdamon | core:planeswalker: 875 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 15–19 (cap 19) · core:airElemental 3–3 (cap 3) · core:waterElemental 2–3 (cap 3) |
| core:fiur | core:planeswalker: 875 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 15–19 (cap 19) · core:airElemental 3–3 (cap 3) · core:waterElemental 2–3 (cap 3) |
| core:kalt | core:planeswalker: 875 | core:pixie 15–25 · core:waterElemental 3–5 · core:waterElemental 1–2 | core:pixie 15–19 (cap 19) · core:waterElemental 3–3 (cap 3) · core:waterElemental 1–2 (cap 3) |
| core:luna | core:elementalist: 650 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 14–14 (cap 14) · core:airElemental 2–2 (cap 2) · core:waterElemental 2–2 (cap 2) |
| core:brissa | core:elementalist: 650 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 14–14 (cap 14) · core:airElemental 2–2 (cap 2) · core:waterElemental 2–2 (cap 2) |
| core:ciele | core:elementalist: 650 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 14–14 (cap 14) · core:airElemental 2–2 (cap 2) · core:waterElemental 2–2 (cap 2) |
| core:labetha | core:elementalist: 650 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 14–14 (cap 14) · core:airElemental 2–2 (cap 2) · core:waterElemental 2–2 (cap 2) |
| core:inteus | core:elementalist: 650 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 14–14 (cap 14) · core:airElemental 2–2 (cap 2) · core:waterElemental 2–2 (cap 2) |
| core:aenain | core:elementalist: 650 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 14–14 (cap 14) · core:airElemental 2–2 (cap 2) · core:waterElemental 2–2 (cap 2) |
| core:gelare | core:elementalist: 650 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 14–14 (cap 14) · core:airElemental 2–2 (cap 2) · core:waterElemental 2–2 (cap 2) |
| core:grindan | core:elementalist: 650 | core:pixie 15–25 · core:airElemental 3–5 · core:waterElemental 2–3 | core:pixie 14–14 (cap 14) · core:airElemental 2–2 (cap 2) · core:waterElemental 2–2 (cap 2) |
