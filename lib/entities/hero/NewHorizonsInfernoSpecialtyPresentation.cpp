/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsInfernoSpecialtyPresentation.h"
#include "NewHorizonsInfernoSpecialties.h"
#include "NewHorizonsInfernoAdventureSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsSpecialtyArtwork.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"

namespace newHorizonsHeroes
{
namespace
{
std::string infernoPercentText(int basisPoints)
{
	return std::to_string(basisPoints / 100)
		+ (basisPoints % 100 ? "." + std::to_string(basisPoints % 100 / 10) : "") + "%";
}
}

bool resolveInfernoSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * captured, SpecialtyPresentation & result)
{
	const auto rules = infernoSpecialtyRules(development);
	const auto key = hero.getJsonKey();
	const int level = captured ? std::max(1, static_cast<int>(captured->level)) : 1;
	if(key == "core:octavia" && rules.cutpurse)
	{
		const auto & value = *rules.cutpurse;
		const int percent = std::min(value.maximumPercent * 100,
			value.basePercent * 100 + (level - 1) * value.percentPerLevelBasisPoints);
		result.name = "Cutpurse";
		result.artwork = detail::resolvedSpecialtyArtwork(key, detail::SpecialtyArtworkFamily::Resource, "gold");
		result.description = "When Octavia defeats an enemy hero, she steals " + infernoPercentText(percent)
			+ " of that player's Gold treasury. The share begins at " + std::to_string(value.basePercent)
			+ "%, grows by " + infernoPercentText(value.percentPerLevelBasisPoints)
			+ " per further hero level, and reaches at most " + std::to_string(value.maximumPercent) + "%.";
	}
	else if(key == "core:calid" && rules.sulfurSense)
	{
		const auto & value = *rules.sulfurSense;
		result.name = "Sulfur Sense";
		result.artwork = detail::resolvedSpecialtyArtwork(key, detail::SpecialtyArtworkFamily::Resource, "sulfur");
		result.description = "Each resource pile Calid picks up also grants "
			+ std::to_string(sulfurSenseAmount(value, level)) + " Sulfur, after ordinary resource multipliers. Starts at "
			+ std::to_string(value.baseSulfur) + ", plus 1 per "
			+ std::to_string(value.levelsPerAdditionalSulfur) + " hero levels.";
	}
	else if(key == "core:xyron" && rules.kindling)
	{
		const auto & value = *rules.kindling;
		const int cap = std::min(value.maximumPercent * 100, value.capPermillePerLevel * level * 10);
		result.name = "Kindling";
		result.artwork = detail::resolvedSpecialtyArtwork(key, detail::SpecialtyArtworkFamily::Spell, "core:inferno");
		result.spell = SpellID::INFERNO;
		result.description = "Each Fire spell Xyron casts in combat makes the Spell Power effects of his next Fire spell "
			+ std::to_string(value.percentPerCast) + "% stronger, cumulatively, up to " + infernoPercentText(cap)
			+ " at his current level. The cap grows by " + infernoPercentText(value.capPermillePerLevel * 10)
			+ " per hero level, to at most " + std::to_string(value.maximumPercent)
			+ "%. Fixed spell values are unchanged. Casting any other spell resets the bonus.";
	}
	else if(key == "core:olema" && rules.markedForTheKill)
	{
		const auto & value = *rules.markedForTheKill;
		result.name = "Marked for the Kill";
		result.artwork = detail::resolvedSpecialtyArtwork(key, detail::SpecialtyArtworkFamily::Spell, "new-horizons:frailty");
		result.description = "At combat start, Olema casts Frailty for free on the "
			+ std::to_string(value.stacks + level / value.levelsPerStack)
			+ " strongest eligible enemy stacks, using her Spell Power. Strength is ranked by total Army Value. Starts at " + std::to_string(value.stacks)
			+ " stack, plus 1 per " + std::to_string(value.levelsPerStack) + " hero levels.";
	}
	else if(key == "core:ash" && rules.bloodfire)
	{
		const auto & value = *rules.bloodfire;
		result.name = "Bloodfire";
		result.artwork = detail::resolvedSpecialtyArtwork(key, detail::SpecialtyArtworkFamily::Spell, "core:fireball");
		result.spell = SpellID::FIREBALL;
		result.description = "Friendly stacks anywhere in Ash's Fireball area take no Fireball damage and gain +"
			+ std::to_string(std::min(value.maximumAttack, value.attack + level / 5 * value.attackPerFiveLevels))
			+ " Attack for " + std::to_string(value.duration) + " rounds instead. Starts at +"
			+ std::to_string(value.attack) + ", plus " + std::to_string(value.attackPerFiveLevels)
			+ " per five hero levels, capped at +" + std::to_string(value.maximumAttack) + ".";
	}
	else if(key == "core:xarfax" && rules.burnAway)
	{
		const auto & value = *rules.burnAway;
		result.name = "Burn Away";
		result.artwork = detail::resolvedSpecialtyArtwork(key, detail::SpecialtyArtworkFamily::Spell, "core:fireball");
		result.spell = SpellID::FIREBALL;
		result.description = "Xarfax's Fireball removes up to "
			+ std::to_string(std::min(value.maximumEffects, value.effects + level / value.levelsPerEffect))
			+ " most recent beneficial spell effects from every enemy stack it hits. Starts at "
			+ std::to_string(value.effects) + ", plus 1 per " + std::to_string(value.levelsPerEffect)
			+ " hero levels, capped at " + std::to_string(value.maximumEffects) + ".";
	}
	else if(key == "core:pyre" && rules.hellfireBattery)
	{
		const auto & value = *rules.hellfireBattery;
		result.name = "Hellfire Battery";
		result.artwork = detail::resolvedSpecialtyArtwork(key, detail::SpecialtyArtworkFamily::Creature, "core:ballista");
		result.creature = CreatureID::BALLISTA;
		result.description = infernoPercentText(value.at(level) * 10)
			+ " of Pyre's Ballista damage becomes Fire magical damage. It remains one shot with one total damage budget. "
			+ "The Fire share begins at " + infernoPercentText(value.base * 10) + ", grows by "
			+ infernoPercentText(value.perLevel * 10) + " per further hero level, and reaches at most "
			+ infernoPercentText(value.maximum * 10) + ".";
	}
	else
		return false;
	result.summary = result.description;
	return true;
}
}
