/*
 * HistoricalNecromancyFixtureData.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 * Test-only captured JSON; generated from config/newHorizonsPerks.json at
 * a08ffecbaec569516cbdd1067d73139ca7316e9d.
 * Raw JSON payload SHA256: 920518e6ad4005c479ecbb72ffc56c9f23bf6929b233d7474a4455cb7053e82b
 */
#pragma once

namespace necromancyTest
{
inline constexpr char historicalNecromancyCapture[] = R"NH_JSON({
"sourceRevision": "a08ffecbaec569516cbdd1067d73139ca7316e9d",
"skill": {
			"id": "new-horizons:necromancy",
			"name": "Necromancy",
			"domain": "Faction",
			"availability": "Necropolis heroes only",
			"description": "Faction-locked Skill. Necromancy converts eligible enemy casualties into permanent Undead after victory. It creates strategic army growth without consuming the hero's Hero Action in combat.",
			"ranks": {
				"basic": {
					"description": "After victory, raise Skeletons equal to 10% of eligible living enemy creature casualties, rounded down.",
					"effect": {
						"status": "active",
						"description": "After victory, raise Skeletons equal to 10% of eligible living enemy creature casualties, rounded down."
					}
				},
				"advanced": {
					"description": "Raise Skeletons equal to 20% of eligible casualties.",
					"effect": {
						"status": "active",
						"description": "Raise Skeletons equal to 20% of eligible casualties."
					}
				},
				"expert": {
					"description": "Raise Skeletons equal to 30% of eligible casualties.",
					"effect": {
						"status": "active",
						"description": "Raise Skeletons equal to 30% of eligible casualties."
					}
				}
			},
			"perks": [
				{
					"id": "new-horizons:necromancy.boneCollector",
					"name": "Bone Collector",
					"requires": "basic",
					"description": "Necromancy raises an additional +5 percentage points of eligible casualties as Skeletons.",
					"effect": {
						"status": "active",
						"description": "Necromancy raises an additional +5 percentage points of eligible casualties as Skeletons."
					}
				},
				{
					"id": "new-horizons:necromancy.corpsePreservation",
					"name": "Corpse Preservation",
					"requires": "basic",
					"description": "Casualties caused by ordinary magical damage become eligible for Necromancy. Without this perk, ordinary magical casualties are excluded. Effects that explicitly destroy or invalidate remains, such as Disintegrate, remain excluded even with the perk.",
					"effect": {
						"status": "active",
						"description": "Casualties caused by ordinary magical damage become eligible for Necromancy. Without this perk, ordinary magical casualties are excluded. Effects that explicitly destroy or invalidate remains, such as Disintegrate, remain excluded even with the perk."
					}
				},
				{
					"id": "new-horizons:necromancy.darkConversion",
					"name": "Dark Conversion",
					"requires": "basic",
					"description": "When resolving Necromancy, every complete group of 3 Skeletons generated from eligible Core-tier casualties is automatically raised as 1 Zombie instead.",
					"effect": {
						"status": "active",
						"description": "When resolving Necromancy, every complete group of 3 Skeletons generated from eligible Core-tier casualties is automatically raised as 1 Zombie instead."
					}
				},
				{
					"id": "new-horizons:necromancy.blackHarvest",
					"name": "Black Harvest",
					"requires": "basic",
					"description": "After Necromancy raises at least 10 creatures, recover 1 Mana per 10 creatures raised, up to 10 Mana.",
					"effect": {
						"status": "active",
						"description": "After Necromancy raises at least 10 creatures, recover 1 Mana per 10 creatures raised, up to 10 Mana."
					}
				},
				{
					"id": "new-horizons:necromancy.soulHarvester",
					"name": "Soul Harvester",
					"requires": "advanced",
					"description": "When resolving Necromancy, every complete group of 6 Skeletons generated from eligible Elite-tier casualties is automatically raised as 1 Wight. Wights remain Core-tier; the conversion's input is Elite-tier casualties, not its output.",
					"effect": {
						"status": "active",
						"description": "When resolving Necromancy, every complete group of 6 Skeletons generated from eligible Elite-tier casualties is automatically raised as 1 Wight. Wights remain Core-tier; the conversion's input is Elite-tier casualties, not its output."
					}
				},
				{
					"id": "new-horizons:necromancy.deathLord",
					"name": "Death Lord",
					"requires": "advanced",
					"description": "Construct and Elemental casualties become eligible at 25% of the normal Necromancy conversion rate.",
					"effect": {
						"status": "active",
						"description": "Construct and Elemental casualties become eligible at 25% of the normal Necromancy conversion rate."
					}
				},
				{
					"id": "new-horizons:necromancy.graveKnowledge",
					"name": "Grave Knowledge",
					"requires": "advanced",
					"description": "When defeating Undead enemies, 20% of their eligible casualties may be reclaimed as Skeletons even though they are not living.",
					"effect": {
						"status": "active",
						"description": "When defeating Undead enemies, 20% of their eligible casualties may be reclaimed as Skeletons even though they are not living."
					}
				},
				{
					"id": "new-horizons:necromancy.ossuary",
					"name": "Ossuary",
					"requires": "advanced",
					"description": "If the hero cannot accept the raised creatures because of unavailable army slots or Leadership limits, they are sent to the nearest owned Necropolis town instead of being lost.",
					"effect": {
						"status": "active",
						"description": "If the hero cannot accept the raised creatures because of unavailable army slots or Leadership limits, they are sent to the nearest owned Necropolis town instead of being lost."
					}
				},
				{
					"id": "new-horizons:necromancy.masterOfBones",
					"name": "Master of Bones",
					"requires": "expert",
					"description": "Skeletons raised by Necromancy are raised as their upgraded form when the appropriate Necropolis upgrade is available to the player.",
					"effect": {
						"status": "active",
						"description": "Skeletons raised by Necromancy are raised as their upgraded form when the appropriate Necropolis upgrade is available to the player."
					}
				},
				{
					"id": "new-horizons:necromancy.lordOfTheDead",
					"name": "Lord of the Dead",
					"requires": "expert",
					"description": "After defeating an army containing at least one Champion-tier living creature, if the base Necromancy result contains at least 12 Skeletons, 12 are automatically replaced with 1 Bone Dragon, once per combat. This conversion resolves before other Necromancy conversion perks. Consume the 12 Skeleton equivalents from Champion/unclassified contributions first, then Elite contributions, then Core contributions; only remaining contributions may fund the later conversions.",
					"effect": {
						"status": "active",
						"description": "After defeating an army containing at least one Champion-tier living creature, if the base Necromancy result contains at least 12 Skeletons, 12 are automatically replaced with 1 Bone Dragon, once per combat. This conversion resolves before other Necromancy conversion perks. Consume the 12 Skeleton equivalents from Champion/unclassified contributions first, then Elite contributions, then Core contributions; only remaining contributions may fund the later conversions."
					}
				}
			]
		}
})NH_JSON";
}
