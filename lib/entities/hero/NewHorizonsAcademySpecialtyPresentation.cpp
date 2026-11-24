/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsAcademySpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsSpecialtyArtwork.h"
#include "NewHorizonsRevisedSpecialties.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"

namespace newHorizonsHeroes
{
namespace
{
/// "5.5%" for 55 permille, "5%" for 50.
std::string academyPermilleText(int permille)
{
	return std::to_string(permille / 10) + (permille % 10 ? "." + std::to_string(permille % 10) : "") + "%";
}

std::string academyGrowthText(const LevelScaledValue & value, bool permille)
{
	const auto show = [permille](int amount) { return permille ? academyPermilleText(amount) : std::to_string(amount) + "%"; };
	return " (" + show(value.base) + " at hero level 1, +" + show(value.perLevel) + " per further level, up to "
		+ show(value.maximum) + ")";
}

using Family = detail::SpecialtyArtworkFamily;

bool academyCombatPresentation(const std::string & key, const AcademySpecialtyRules & rules, int level,
	SpecialtyPresentation & result)
{
	if(key == "core:neela" && rules.hardened)
	{
		const auto & hardened = *rules.hardened;
		const auto total = academyPermilleText(hardened.totalPermille.at(level));
		result.name = "Hardened";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:armorer"));
		result.artwork = detail::resolvedSpecialtyArtwork(key, Family::Skill, "core:armorer");
		result.summary = "Each time one of her stacks is hit, it takes " + academyPermilleText(hardened.hitPermille)
			+ " less damage from later hits until it next acts, up to " + total + ".";
		result.description = "Each creature attack that hits one of this hero's stacks makes it take "
			+ academyPermilleText(hardened.hitPermille) + " less damage from later creature attacks until its next "
			"activation. The reduction adds up to at most " + total + academyGrowthText(hardened.totalPermille, true)
			+ " and ends when the stack acts.";
		return true;
	}
	if(key == "core:astral" && rules.practiceMakesPerfect)
	{
		const auto & practice = *rules.practiceMakesPerfect;
		result.name = "Practice Makes Perfect";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft"));
		result.artwork = detail::resolvedSpecialtyArtwork(key, Family::Spell, "new-horizons:phantomArmy");
		result.summary = "Each spell he casts in combat becomes permanently " + std::to_string(practice.percentPerCast)
			+ "% stronger for him, up to " + std::to_string(practice.capPercentPerLevel * level) + "% per spell.";
		result.description = "Each time this hero casts a spell in combat, that spell's Spell Power effects become "
			"permanently " + std::to_string(practice.percentPerCast) + "% stronger for him. Each spell's bonus is capped at "
			+ std::to_string(practice.capPercentPerLevel) + "% per hero level (currently "
			+ std::to_string(practice.capPercentPerLevel * level) + "%). Fixed spell values are unchanged.";
		return true;
	}
	if(key == "core:solmyr" && rules.stormLord)
	{
		const auto & storm = *rules.stormLord;
		result.name = "Storm Lord";
		result.spell = SpellID(SpellID::CHAIN_LIGHTNING);
		result.artwork = detail::resolvedSpecialtyArtwork(key, Family::Spell, "new-horizons:masterChainLightning");
		result.summary = "Lightning Bolt and Chain Lightning deal " + std::to_string(storm.damagePercent.at(level))
			+ "% more Spell Power damage; he learns Chain Lightning at level " + std::to_string(storm.chainLightningLevel) + ".";
		result.description = "This hero's Lightning Bolt and Chain Lightning deal "
			+ std::to_string(storm.damagePercent.at(level)) + "% more damage from Spell Power"
			+ academyGrowthText(storm.damagePercent, false) + ", in addition to Stormcaller. He learns Chain Lightning when he "
			"reaches hero level " + std::to_string(storm.chainLightningLevel) + ".";
		return true;
	}
	return false;
}

bool academyChangedPresentation(const std::string & key, const AcademySpecialtyRules & rules, int level,
	SpecialtyPresentation & result)
{
	if(key == "core:fafner" && rules.serpentsEmbraceBoundPercent)
	{
		const auto & bound = *rules.serpentsEmbraceBoundPercent;
		result.name = "Serpent’s Embrace";
		result.creature = CreatureID(CreatureID::decode("core:naga"));
		result.artwork = detail::resolvedSpecialtyArtwork(key, Family::Creature, "core:naga");
		result.summary = "Nagas and Naga Queens bind enemies they strike, and deal " + std::to_string(bound.at(level))
			+ "% more damage to targets they hold.";
		result.description = "Damaging primary melee attacks by Nagas and Naga Queens bind their targets, including "
			"retaliation. Each source prevents voluntary movement and Teleport until its next activation or death. "
			"Cure and Dispel do not remove this physical restraint. A Naga stack deals " + std::to_string(bound.at(level))
			+ "% more damage" + academyGrowthText(bound, false) + " to a target it has bound.";
		return true;
	}
	if(key == "core:cyra" && rules.focusMagicPermille)
	{
		const auto & focus = *rules.focusMagicPermille;
		const SpellID spell(SpellID::decode("new-horizons:focusMagic"));
		result.name = "Focus Magic";
		result.spell = spell;
		result.artwork = detail::resolvedSpecialtyArtwork(key, Family::Spell, "core:haste");
		result.summary = "Focus Magic grants " + std::to_string(focus.at(level) / 10)
			+ " more percentage points of ranged damage; its three charges are unchanged.";
		result.description = "Focus Magic cast by this hero grants +" + std::to_string(focus.at(level) / 10)
			+ " additional percentage points of ranged damage (10 at hero level 1, +0.5 per further level, rounded down, "
			"up to " + std::to_string(focus.maximum / 10) + "). This stacks with Arcane Arsenal.";
		return true;
	}
	return false;
}

bool academyAdventurePresentation(const std::string & key, const AcademySpecialtyRules & rules, int level,
	SpecialtyPresentation & result)
{
	if(key == "core:rissa" && rules.spoilsOfAlchemy)
	{
		const auto & spoils = *rules.spoilsOfAlchemy;
		const int divisor = std::max(spoils.minimumGoldPerMercury, spoils.goldPerMercury - spoils.goldPerMercuryPerLevel * level);
		result.name = "Spoils of Alchemy";
		result.artwork = detail::resolvedSpecialtyArtwork(key, Family::Resource, "mercury");
		result.summary = "After each won combat she gains 1 Mercury per " + std::to_string(divisor)
			+ " Gold of enemy creatures destroyed, at most " + std::to_string(spoils.maximumMercury) + ".";
		result.description = "After each combat this hero wins, she gains 1 Mercury for every " + std::to_string(divisor)
			+ " Gold of recruitment cost of the enemy creatures destroyed (" + std::to_string(spoils.goldPerMercury) + " - "
			+ std::to_string(spoils.goldPerMercuryPerLevel) + " per hero level, at least "
			+ std::to_string(spoils.minimumGoldPerMercury) + "), at most " + std::to_string(spoils.maximumMercury)
			+ " per combat. Summoned creatures and war machines do not count.";
		return true;
	}
	if(key == "core:aine" && rules.wellspringOfWealth)
	{
		const auto & wealth = *rules.wellspringOfWealth;
		const int permille = std::min(wealth.maximumGoldPerManaPermille,
			wealth.goldPerManaPermille + wealth.goldPerManaPermillePerLevel * level);
		const auto multiplier = [](int value)
		{
			std::string fraction = std::to_string(value % 1000 + 1000).substr(1);
			while(!fraction.empty() && fraction.back() == '0')
				fraction.pop_back();
			return std::to_string(value / 1000) + (fraction.empty() ? "" : "." + fraction);
		};
		result.name = "Wellspring of Wealth";
		result.artwork = detail::resolvedSpecialtyArtwork(key, Family::Resource, "gold");
		result.summary = "At the start of each day she gains Gold equal to her Mana x " + multiplier(permille) + ".";
		result.description = "At the start of each day this hero gains Gold equal to the Mana she holds x "
			+ multiplier(permille) + " (x" + multiplier(wealth.goldPerManaPermille) + " + "
			+ multiplier(wealth.goldPerManaPermillePerLevel) + " per hero level, up to x"
			+ multiplier(wealth.maximumGoldPerManaPermille) + "), counted before that day's Mana recovery. "
			"Mana spent in combat is income lost.";
		return true;
	}
	if(key == "core:josephine" && rules.goldStandardDiscountPercent)
	{
		const auto & discount = *rules.goldStandardDiscountPercent;
		result.name = "Gold Standard";
		result.creature = CreatureID(CreatureID::decode("core:stoneGolem"));
		result.artwork = "newHorizons/heroSpecialties/JosephineFieldReassembly";
		result.summary = "She can upgrade Iron Golems into Gold Golems, for " + std::to_string(discount.at(level)) + "% less.";
		result.description = "This hero can upgrade Iron Golems into Gold Golems through ordinary upgrade, ownership and "
			"Leadership-capacity checks. That upgrade costs " + std::to_string(discount.at(level)) + "% less"
			+ academyGrowthText(discount, false) + ", rounded down for each resource.";
		return true;
	}
	if(key == "core:thane" && rules.sageOfTheAges)
	{
		const auto & sage = *rules.sageOfTheAges;
		const auto gains = "+" + std::to_string(sage.spellPowerPerLevel) + " Spell Power and +"
			+ std::to_string(sage.knowledgePerLevel) + " Knowledge";
		result.name = "Sage of the Ages";
		result.artwork = "newHorizons/heroSpecialties/ThaneSageOfTheAges";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:learning"));
		result.summary = "Gain " + gains + " on each level gained, in addition to ordinary growth.";
		result.description = "Gain " + gains + " ratings on each level gained, in addition to normal class and Skill growth. "
			"No additional ratings are granted at level 1.";
		return true;
	}
	return false;
}

/// Theodorus's Inexhaustible Knowledge and, after the same capture, Halon's Nullifying Shot.
bool academySwapPresentation(const std::string & key, const JsonNode & development, const AcademySpecialtyRules & rules,
	int level, SpecialtyPresentation & result)
{
	constexpr auto NULLIFYING_ART = "newHorizons/heroSpecialties/TheodorusMeasuredVolley";
	constexpr auto KNOWLEDGE_ART = "newHorizons/heroSpecialties/TheodorusInexhaustibleKnowledge";
	if(key == "core:theodorus" && rules.inexhaustibleKnowledge)
	{
		const auto & knowledge = *rules.inexhaustibleKnowledge;
		const auto share = academyPermilleText(knowledge.manaPermille.at(level));
		result.name = "Inexhaustible Knowledge";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
		result.artwork = KNOWLEDGE_ART;
		result.summary = "At the start of each combat round after the first he regains " + share + " of his maximum Mana.";
		result.description = "At the start of each combat round after the first, this hero regains " + share
			+ " of his maximum Mana" + academyGrowthText(knowledge.manaPermille, true) + ", rounded down and at least "
			+ std::to_string(knowledge.minimumMana) + ", never above his maximum.";
		return true;
	}
	if(key != "core:halon" || !rules.halonNullifyingShot)
		return false;
	const auto & ordinary = development["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	const auto chance = [&](const char * coefficient)
	{
		const int authored = static_cast<int>(ordinary[coefficient].Integer());
		const auto scaling = specialtyLevelScaling(development);
		return scaling ? levelScaledOrdinaryValue(*scaling, coefficient, authored, level) : authored;
	};
	result.name = "Nullifying Shot";
	result.creature = CreatureID(CreatureID::decode("core:mage"));
	result.artwork = NULLIFYING_ART;
	result.summary = "Successful Mage shots have a " + std::to_string(chance("dispellingShotMageChancePercent"))
		+ "% chance to dispel positive temporary magic; Arch Mage shots " + std::to_string(chance("dispellingShotArchMageChancePercent"))
		+ "%.";
	result.description = "Each successful ranged attack by Magi has a " + std::to_string(chance("dispellingShotMageChancePercent"))
		+ "% chance to dispel every positive temporary magical effect from its target; Arch Magi have a "
		+ std::to_string(chance("dispellingShotArchMageChancePercent")) + "% chance. These chances grow by 3% of themselves per "
		"hero level after the first.";
	return true;
}
}

bool resolveAcademySpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	const std::string key = hero.getJsonKey();
	const auto rules = academySpecialtyRules(development);
	const int level = capturedHero ? std::max(1, static_cast<int>(capturedHero->level)) : 1;
	return academyCombatPresentation(key, rules, level, result) || academyChangedPresentation(key, rules, level, result)
		|| academyAdventurePresentation(key, rules, level, result) || academySwapPresentation(key, development, rules, level, result);
}
}
