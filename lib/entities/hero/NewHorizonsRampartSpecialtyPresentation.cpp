/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRampartSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsSpecialtyArtwork.h"
#include "CHero.h"
#include "../../battle/NewHorizonsForestAmbush.h"
#include "../../mapObjects/CGHeroInstance.h"

namespace newHorizonsHeroes
{
namespace
{
enum class Unit { Percent, Permille, BasisPoints };

/// "5.5%" for 55 permille, "2.25%" for 225 basis points, "43%" for 43 percent.
std::string amountText(int value, Unit unit)
{
	const int divisor = unit == Unit::BasisPoints ? 100 : unit == Unit::Permille ? 10 : 1;
	std::string fraction = std::to_string(value % divisor);
	if(divisor == 100 && fraction.size() == 1)
		fraction = "0" + fraction;
	while(!fraction.empty() && fraction.back() == '0')
		fraction.pop_back();
	return std::to_string(value / divisor) + (divisor > 1 && !fraction.empty() ? "." + fraction : "") + "%";
}

std::string growthText(const LevelScaledValue & value, Unit unit)
{
	return " (" + amountText(value.base, unit) + " at hero level 1, +" + amountText(value.perLevel, unit)
		+ " per further level, up to " + amountText(value.maximum, unit) + ")";
}

std::optional<std::string> artwork(const std::string & hero, detail::SpecialtyArtworkFamily family, const char * target)
{
	return detail::resolvedSpecialtyArtwork(hero, family, target);
}

using Family = detail::SpecialtyArtworkFamily;

bool combatPresentation(const std::string & key, const RampartSpecialtyRules & rules, int level, SpecialtyPresentation & result)
{
	if(key == "core:mephala" && rules.useTheGroundPermille)
	{
		const auto & value = *rules.useTheGroundPermille;
		result.name = "Use the Ground";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:armorer"));
		result.artwork = artwork(key, Family::Skill, "core:armorer");
		result.summary = "Her stacks next to an obstacle or a castle wall take " + amountText(value.at(level), Unit::Permille)
			+ " less physical damage.";
		result.description = "This hero's stacks take " + amountText(value.at(level), Unit::Permille)
			+ " less physical damage" + growthText(value, Unit::Permille) + " while they stand next to a battlefield "
			"obstacle or a standing castle wall. Each blow checks the position when it lands.";
		return true;
	}
	if(key == "core:coronius" && rules.giantSlayerPermillePerStep)
	{
		const auto & value = *rules.giantSlayerPermillePerStep;
		const auto current = amountText(value.at(level), Unit::Permille);
		result.name = "Giant-Slayer";
		result.spell = SpellID(SpellID::decode("new-horizons:holyWrath"));
		result.artwork = artwork(key, Family::Spell, "new-horizons:holyWrath");
		result.summary = "His creatures deal " + current + " more damage for each rank the target is above them.";
		result.description = "This hero's creatures deal " + current + " more damage" + growthText(value, Unit::Permille)
			+ " for each rank the target is above the attacker (Core, Elite, Champion). His Holy Wrath deals "
			+ current + " more for each rank its target is above Core.";
		return true;
	}
	if(key == "core:alagar" && rules.brittlePermille)
	{
		const auto & value = *rules.brittlePermille;
		result.name = "Brittle";
		result.spell = SpellID(SpellID::ICE_BOLT);
		result.artwork = artwork(key, Family::Spell, "core:iceBolt");
		result.summary = "His ice spells end the Defend of enemy stacks they hit, which then take "
			+ amountText(value.at(level), Unit::Permille) + " more physical damage.";
		result.description = "Each enemy stack this hero's Ice Bolt or Frost Ring hits loses its Defend. Until its next "
			"activation it cannot Defend and takes " + amountText(value.at(level), Unit::Permille)
			+ " more physical damage" + growthText(value, Unit::Permille) + ".";
		return true;
	}
	if(key == "core:melodia" && rules.againstTheOdds)
	{
		const auto & odds = *rules.againstTheOdds;
		const int luck = std::min(odds.maximumLuck, odds.luck + level / odds.levelsPerLuck);
		result.name = "Against the Odds";
		result.spell = SpellID(SpellID::BLESS);
		result.artwork = artwork(key, Family::Spell, "core:bless");
		result.summary = "When the enemy army is stronger than hers, all her stacks gain +" + std::to_string(luck) + " Luck.";
		result.description = "When a combat starts with the enemy army stronger than hers, all this hero's stacks gain +"
			+ std::to_string(luck) + " Luck for that combat (+" + std::to_string(odds.luck) + ", +1 for every "
			+ std::to_string(odds.levelsPerLuck) + " hero levels, up to +" + std::to_string(odds.maximumLuck)
			+ "). Strength compares the total value of the creatures on each side.";
		return true;
	}
	return false;
}

bool magicPresentation(const std::string & key, const RampartSpecialtyRules & rules, int level, SpecialtyPresentation & result)
{
	if(key == "core:uland" && rules.livingRemedy)
	{
		const auto & remedy = *rules.livingRemedy;
		const auto current = amountText(remedy.healingBasisPoints.at(level), Unit::BasisPoints);
		result.name = "Living Remedy";
		result.spell = SpellID(SpellID::CURE);
		result.artwork = artwork(key, Family::Spell, "core:cure");
		result.summary = "His Cure also gives Regeneration for " + std::to_string(remedy.rounds) + " rounds, healing "
			+ current + " of the stack's Health each round.";
		result.description = "Each stack this hero Cures also regenerates for " + std::to_string(remedy.rounds)
			+ " rounds: at the start of its activation it heals " + current + " of its full Health"
			+ growthText(remedy.healingBasisPoints, Unit::BasisPoints) + ". Regeneration heals wounds only; it never "
			"restores lost creatures.";
		return true;
	}
	if(key == "core:gem" && rules.sorceressOfEnroth)
	{
		const auto & sorceress = *rules.sorceressOfEnroth;
		const int spellLevel = std::min(5, sorceress.baseSpellLevel + level / sorceress.heroLevelsPerSpellLevel);
		result.name = "Sorceress of Enroth";
		result.artwork = "newHorizons/heroSpecialties/GemRestorativeSorceress";
		result.summary = "Her first spell each combat costs no Mana when it is level " + std::to_string(spellLevel)
			+ " or lower.";
		result.description = "The first spell this hero casts in each combat costs no Mana when its spell level is at most "
			+ std::to_string(spellLevel) + " (" + std::to_string(sorceress.baseSpellLevel) + ", +1 for every "
			+ std::to_string(sorceress.heroLevelsPerSpellLevel) + " hero levels, up to 5). A higher-level first spell "
			"costs its normal Mana. Arcane Focus still strengthens the first spell.";
		return true;
	}
	if(key == "core:aeris" && rules.perfectMountPercent)
	{
		const auto & value = *rules.perfectMountPercent;
		const int current = value.at(level);
		result.name = "Perfect Mount";
		result.creature = CreatureID(CreatureID::decode("core:pegasus"));
		result.artwork = "newHorizons/heroSpecialties/AerisWingbeat";
		result.summary = current >= 100 ? "Pegasi and Silver Pegasi are immune to magical damage."
			: "Pegasi and Silver Pegasi take " + std::to_string(current) + "% less magical damage.";
		result.description = "Pegasi and Silver Pegasi take " + std::to_string(current) + "% less magical damage"
			+ growthText(value, Unit::Percent) + "; at 100% they are immune to it. The ordinary 95% cap on magical "
			"damage reduction applies below immunity. Non-damaging magic remains eligible.";
		return true;
	}
	return false;
}

bool adventurePresentation(const std::string & key, const RampartSpecialtyRules & rules, int level, SpecialtyPresentation & result)
{
	if(key == "core:kyrre" && rules.elusive)
	{
		result.name = "Elusive";
		result.skill = SecondarySkill(SecondarySkill::LOGISTICS);
		result.artwork = artwork(key, Family::Skill, "core:logistics");
		const char * rank = level >= rules.elusive->championLevel ? "all neutral guards"
			: level >= rules.elusive->eliteLevel ? "Core and Elite neutral guards" : "Core neutral guards";
		result.summary = std::string("She can move next to ") + rank + " without being attacked.";
		result.description = "This hero can move next to neutral guards without being attacked: Core guards from hero "
			"level 1, Elite guards from level " + std::to_string(rules.elusive->eliteLevel) + " and Champion guards from level "
			+ std::to_string(rules.elusive->championLevel) + ". Stepping onto a guard still starts the fight.";
		return true;
	}
	if(key == "core:jenova" && rules.patronessPermille)
	{
		const auto & value = *rules.patronessPermille;
		result.name = "Patroness";
		result.artwork = artwork(key, Family::Resource, "gold");
		result.summary = "The first time each week she enters each of her towns, she collects "
			+ amountText(value.at(level), Unit::Permille) + " of its daily income.";
		result.description = "The first time each week this hero enters each town her player owns, she collects "
			+ amountText(value.at(level), Unit::Permille) + " of that town's daily income" + growthText(value, Unit::Permille)
			+ ", rounded down for each resource.";
		return true;
	}
	return false;
}

bool changedPresentation(const std::string & key, const RampartSpecialtyRules & rules, int level, SpecialtyPresentation & result)
{
	if(key == "core:ryland" && rules.treantmootPercent)
	{
		const auto & value = *rules.treantmootPercent;
		result.name = "Treantmoot";
		result.creature = CreatureID(CreatureID::decode("core:dendroidGuard"));
		result.artwork = "newHorizons/heroSpecialties/RylandLivingBastion";
		result.summary = "Friendly non-Dendroid stacks beside a same-army Dendroid cannot be deliberately targeted by enemy "
			"creature attacks; Dendroids take " + std::to_string(value.at(level)) + "% less physical damage.";
		result.description = "Friendly stacks other than Dendroid Guards and Dendroid Soldiers cannot be deliberately targeted "
			"by enemy creature attacks while adjacent to a same-army Dendroid. Multiple sources do not stack; mind control "
			"does not transfer this aura's army affiliation. Dendroid Guards and Dendroid Soldiers take "
			+ std::to_string(value.at(level)) + "% less physical damage" + growthText(value, Unit::Percent) + ".";
		return true;
	}
	if(key == "core:ivor" && rules.forestAmbushShotPercent)
	{
		const auto & value = *rules.forestAmbushShotPercent;
		const auto luck = std::to_string(newHorizonsForestAmbush::FIRST_SHOT_LUCK);
		result.name = "Forest Ambush";
		result.creature = CreatureID(CreatureID::decode("core:woodElf"));
		result.artwork = "newHorizons/heroSpecialties/IvorForestAmbush";
		result.summary = "Wood Elves and Grand Elves that Wait before being attacked hide until acting; their first delayed "
			"shot gains +" + luck + " Luck and +" + std::to_string(value.at(level)) + "% damage.";
		result.description = "Wood Elves and Grand Elves that Wait before receiving an attack this round become concealed "
			"until their delayed activation. Concealment prevents deliberate hostile attacks and single-target spells; area "
			"and indirect effects still apply. Incoming attacks and outgoing physical creature attacks reveal them. Their "
			"first primary physical ranged strike from concealment or in that delayed activation gains +" + luck
			+ " Luck within normal limits and +" + std::to_string(value.at(level)) + "% damage (+"
			+ std::to_string(value.perLevel) + "% per hero level, up to " + std::to_string(value.maximum)
			+ "%). The bonus expires when that activation ends.";
		return true;
	}
	return false;
}
}

bool resolveRampartSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	const auto rules = rampartSpecialtyRules(development);
	const std::string key = hero.getJsonKey();
	const int level = capturedHero ? std::max(1, static_cast<int>(capturedHero->level)) : 1;
	return combatPresentation(key, rules, level, result) || magicPresentation(key, rules, level, result)
		|| adventurePresentation(key, rules, level, result) || changedPresentation(key, rules, level, result);
}
}
