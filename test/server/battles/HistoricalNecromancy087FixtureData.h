/*
 * HistoricalNecromancy087FixtureData.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 * Test-only captured JSON; generated from config/newHorizonsPerks.json at
 * 51d6dc8eb832db028ec8ddb5a9df38b0e0342c55.
 * Raw JSON payload SHA256: 4f92ad214b1f6f019e2d2eefb385c197b00c10215e912ef1b416b1b9bc51635e
 */
#pragma once

namespace necromancy087Test
{
inline constexpr char capturedRegistryRows[] = R"NH_JSON({
	"sourceRevision": "51d6dc8eb832db028ec8ddb5a9df38b0e0342c55",
	"skill": {
		"id": "new-horizons:necromancy",
		"name": "Necromancy",
		"domain": "Faction",
		"availability": "Necropolis heroes only",
		"description": "Faction-locked Skill. Necromancy raises permanent Skeletons from eligible enemy casualties after victory, without consuming a Hero Action. Skeletons and Skeleton Warriors commanded by this hero require less Leadership. Ordinary magical casualties are eligible; explicitly destroyed or invalid remains are excluded.",
		"ranks": {
			"basic": {
				"description": "After victory, raise Skeletons equal to 10% of eligible living enemy casualties, rounded down. Skeletons and Skeleton Warriors commanded by this hero require 50% less Leadership; round their discounted requirement up to a whole point, minimum 1.",
				"effect": {
					"status": "active",
					"description": "After victory, raise Skeletons equal to 10% of eligible living enemy casualties, rounded down. Skeletons and Skeleton Warriors commanded by this hero require 50% less Leadership; round their discounted requirement up to a whole point, minimum 1."
				}
			},
			"advanced": {
				"description": "After victory, raise Skeletons equal to 20% of eligible living enemy casualties, rounded down. Skeletons and Skeleton Warriors commanded by this hero require 60% less Leadership; round their discounted requirement up to a whole point, minimum 1.",
				"effect": {
					"status": "active",
					"description": "After victory, raise Skeletons equal to 20% of eligible living enemy casualties, rounded down. Skeletons and Skeleton Warriors commanded by this hero require 60% less Leadership; round their discounted requirement up to a whole point, minimum 1."
				}
			},
			"expert": {
				"description": "After victory, raise Skeletons equal to 30% of eligible living enemy casualties, rounded down. Skeletons and Skeleton Warriors commanded by this hero require 70% less Leadership; round their discounted requirement up to a whole point, minimum 1.",
				"effect": {
					"status": "active",
					"description": "After victory, raise Skeletons equal to 30% of eligible living enemy casualties, rounded down. Skeletons and Skeleton Warriors commanded by this hero require 70% less Leadership; round their discounted requirement up to a whole point, minimum 1."
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
				"id": "new-horizons:necromancy.masterOfBones",
				"name": "Bone and Steel",
				"requires": "basic",
				"description": "Skeletons raised by Necromancy are raised as their upgraded form when the appropriate Necropolis upgrade is available to the player.",
				"effect": {
					"status": "active",
					"description": "Skeletons raised by Necromancy are raised as their upgraded form when the appropriate Necropolis upgrade is available to the player."
				}
			},
			{
				"id": "new-horizons:necromancy.darkConversion",
				"name": "Boneward",
				"requires": "basic",
				"description": "Skeletons and Skeleton Warriors commanded by this hero take 30% less magical damage.",
				"effect": {
					"status": "active",
					"description": "Skeletons and Skeleton Warriors commanded by this hero take 30% less magical damage."
				}
			},
			{
				"id": "new-horizons:necromancy.blackHarvest",
				"name": "Soul Harvester",
				"requires": "basic",
				"description": "After Necromancy raises at least 10 new creatures, recover 1 Mana per 10 newly raised creatures, up to 10 Mana. Grave Knowledge recovery does not count.",
				"effect": {
					"status": "active",
					"description": "After Necromancy raises at least 10 new creatures, recover 1 Mana per 10 newly raised creatures, up to 10 Mana. Grave Knowledge recovery does not count."
				}
			},
			{
				"id": "new-horizons:necromancy.soulHarvester",
				"name": "Tomb Guard",
				"requires": "advanced",
				"description": "Skeletons and Skeleton Warriors commanded by this hero gain +1 retaliation per round. Normal retaliation restrictions still apply.",
				"effect": {
					"status": "active",
					"description": "Skeletons and Skeleton Warriors commanded by this hero gain +1 retaliation per round. Normal retaliation restrictions still apply."
				}
			},
			{
				"id": "new-horizons:necromancy.deathLord",
				"name": "Graveplate",
				"requires": "advanced",
				"description": "Skeletons and Skeleton Warriors commanded by this hero take 20% less physical damage.",
				"effect": {
					"status": "active",
					"description": "Skeletons and Skeleton Warriors commanded by this hero take 20% less physical damage."
				}
			},
			{
				"id": "new-horizons:necromancy.graveKnowledge",
				"name": "Grave Knowledge",
				"requires": "advanced",
				"description": "After victory, restore 20% of this hero's own eligible fallen Skeletons and Skeleton Warriors in their original forms, rounded down separately for each form. Destroyed remains, summoned creatures and clones are excluded. This recovery does not count as newly raised creatures for Soul Harvester.",
				"effect": {
					"status": "active",
					"description": "After victory, restore 20% of this hero's own eligible fallen Skeletons and Skeleton Warriors in their original forms, rounded down separately for each form. Destroyed remains, summoned creatures and clones are excluded. This recovery does not count as newly raised creatures for Soul Harvester."
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
				"id": "new-horizons:necromancy.corpsePreservation",
				"name": "Death March",
				"requires": "expert",
				"description": "Skeletons and Skeleton Warriors commanded by this hero gain +2 Speed. Initiative is unchanged.",
				"effect": {
					"status": "active",
					"description": "Skeletons and Skeleton Warriors commanded by this hero gain +2 Speed. Initiative is unchanged."
				}
			},
			{
				"id": "new-horizons:necromancy.lordOfTheDead",
				"name": "Lord of the Dead",
				"requires": "expert",
				"description": "Skeletons and Skeleton Warriors commanded by this hero require 90% less Leadership. This replaces Necromancy's rank-based reduction; round the discounted requirement up to a whole point, minimum 1.",
				"effect": {
					"status": "active",
					"description": "Skeletons and Skeleton Warriors commanded by this hero require 90% less Leadership. This replaces Necromancy's rank-based reduction; round the discounted requirement up to a whole point, minimum 1."
				}
			}
		]
	},
	"policy": {
		"version": 1,
		"skeletonLeadershipReductionPercent": [
			0,
			50,
			60,
			70
		],
		"lordOfTheDeadLeadershipReductionPercent": 90,
		"deathMarchSpeed": 2,
		"bonewardMagicalReductionPercent": 30,
		"tombGuardAdditionalRetaliations": 1,
		"graveplatePhysicalReductionPercent": 20,
		"graveKnowledgeRecoveryPercent": 20
	}
})NH_JSON";
}
