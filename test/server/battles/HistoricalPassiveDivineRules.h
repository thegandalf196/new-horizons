/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../../lib/json/JsonNode.h"

/// Complete authored policy-two Divine Mandate row captured at d0647f2e5d85.
/// This is a local characterization contract, not an older-save support claim.
inline void captureHistoricalPassiveDivineRules(JsonNode & rules)
{
	const std::string text = R"DIVINE_V2({
	"divineMandatePolicy": {
		"version": 2
	},
	"skill": {
		"id": "new-horizons:divineMandate",
		"name": "Divine Mandate",
		"domain": "Faction",
		"availability": "Castle heroes only",
		"description": "Orders can automatically cast a useful beneficial Light spell you know, free of Mana and additional Hero Actions. Rank improves attribute-derived Order effects and the number of free spell activations.",
		"ranks": {
			"basic": {
				"description": "Strengthen attribute-based Order effects by 5 percentage points; up to 1 Orders each combat also cast a useful known Light blessing for free.",
				"effect": {
					"status": "active",
					"description": "Strengthen attribute-based Order effects by 5 percentage points; up to 1 Orders each combat also cast a useful known Light blessing for free."
				},
				"details": "Orders gain +5 percentage points on attribute-derived effects. Up to 1 Orders per combat also trigger a useful known beneficial Light spell, free of Mana and additional Hero Actions. No useful spell spends no activation."
			},
			"advanced": {
				"description": "Strengthen attribute-based Order effects by 10 percentage points; up to 2 Orders each combat also cast a useful known Light blessing for free.",
				"effect": {
					"status": "active",
					"description": "Strengthen attribute-based Order effects by 10 percentage points; up to 2 Orders each combat also cast a useful known Light blessing for free."
				},
				"details": "Orders gain +10 percentage points on attribute-derived effects. Up to 2 Orders per combat also trigger a useful known beneficial Light spell, free of Mana and additional Hero Actions. No useful spell spends no activation."
			},
			"expert": {
				"description": "Strengthen attribute-based Order effects by 15 percentage points; up to 3 Orders each combat also cast a useful known Light blessing for free.",
				"effect": {
					"status": "active",
					"description": "Strengthen attribute-based Order effects by 15 percentage points; up to 3 Orders each combat also cast a useful known Light blessing for free."
				},
				"details": "Orders gain +15 percentage points on attribute-derived effects. Up to 3 Orders per combat also trigger a useful known beneficial Light spell, free of Mana and additional Hero Actions. No useful spell spends no activation."
			}
		},
		"perks": [
			{
				"id": "new-horizons:divineMandate.sacredCommand",
				"name": "Sacred Command",
				"requires": "basic",
				"description": "Order recipients gain +1 Morale and +1 Luck for the Order's duration.",
				"effect": {
					"status": "active",
					"description": "Order recipients gain +1 Morale and +1 Luck for the Order's duration."
				}
			},
			{
				"id": "new-horizons:divineMandate.knightlyValor",
				"name": "Knightly Valor",
				"requires": "basic",
				"description": "Order recipients deal 10% more melee damage for the Order's duration.",
				"effect": {
					"status": "active",
					"description": "Order recipients deal 10% more melee damage for the Order's duration."
				}
			},
			{
				"id": "new-horizons:divineMandate.chaplainSReserve",
				"name": "Chaplain's Reserve",
				"requires": "basic",
				"description": "Each successful free Divine Mandate spell restores three Mana.",
				"effect": {
					"status": "active",
					"description": "Each successful free Divine Mandate spell restores three Mana."
				}
			},
			{
				"id": "new-horizons:divineMandate.shieldOfFaith",
				"name": "Shield of Faith",
				"requires": "basic",
				"description": "Order recipients suffer 10% less damage from enemy attacks for the Order's duration.",
				"effect": {
					"status": "active",
					"description": "Order recipients suffer 10% less damage from enemy attacks for the Order's duration."
				}
			},
			{
				"id": "new-horizons:divineMandate.righteousDiscipline",
				"name": "Righteous Discipline",
				"requires": "advanced",
				"description": "Order recipients gain one additional retaliation for the Order's duration.",
				"effect": {
					"status": "active",
					"description": "Order recipients gain one additional retaliation for the Order's duration."
				}
			},
			{
				"id": "new-horizons:divineMandate.unwaveringStandard",
				"name": "Unwavering Standard",
				"requires": "advanced",
				"description": "Order recipients treat negative Morale as zero for the Order's duration.",
				"effect": {
					"status": "active",
					"description": "Order recipients treat negative Morale as zero for the Order's duration."
				}
			},
			{
				"id": "new-horizons:divineMandate.inspiringLeadership",
				"name": "Inspiring Leadership",
				"requires": "advanced",
				"description": "Recipients of the free Divine Mandate Light spell gain +2 Speed for the issuing Order's scheduled duration. Initiative is unchanged.",
				"effect": {
					"status": "active",
					"description": "Recipients of the free Divine Mandate Light spell gain +2 Speed for the issuing Order's scheduled duration. Initiative is unchanged."
				}
			},
			{
				"id": "new-horizons:divineMandate.consecratedArms",
				"name": "Consecrated Arms",
				"requires": "advanced",
				"description": "Order recipients ignore 10% of their target's Defense in melee for the Order's duration.",
				"effect": {
					"status": "active",
					"description": "Order recipients ignore 10% of their target's Defense in melee for the Order's duration."
				}
			},
			{
				"id": "new-horizons:divineMandate.mandateOfHeaven",
				"name": "Mandate of Heaven",
				"requires": "expert",
				"description": "Divine Mandate can trigger two additional free Light spells each combat, for five at Expert rank.",
				"effect": {
					"status": "active",
					"description": "Divine Mandate can trigger two additional free Light spells each combat, for five at Expert rank."
				}
			},
			{
				"id": "new-horizons:divineMandate.divineFury",
				"name": "Divine Fury",
				"requires": "expert",
				"description": "While under an Order, attacks and retaliations have a 10% chance to cast free Holy Wrath using your hero’s current power and mastery.",
				"effect": {
					"status": "active",
					"description": "While under an Order, attacks and retaliations have a 10% chance to cast free Holy Wrath using your hero’s current power and mastery."
				},
				"details": "While under an Order, melee attacks, ranged attacks and retaliations have a 10% chance to cast free Holy Wrath with your hero’s current power and mastery."
			}
		]
	}
})DIVINE_V2";
	const JsonNode captured(text.data(), text.size(), "historical Divine Mandate policy two");
	rules["divineMandatePolicy"] = captured["divineMandatePolicy"];
	rules["skills"]["new-horizons:divineMandate"] = captured["skill"];
	rules.setOverrideFlag(true); // Do not merge current policy-three fields back in.
}
