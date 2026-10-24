/*
 * NewHorizonsHerbalSurgeonPresentation.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHerbalSurgeon.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/NewHorizonsMagic.h"
namespace newHorizonsHeroes
{
bool resolveHerbalSurgeonPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:verdish")
		return false;
	const auto rule = herbalSurgeonRules(development);
	if(!rule)
		return false;
	result.name = "Herbal Surgeon";
	result.artwork = "newHorizons/heroSpecialties/VerdishHerbalSurgeon";
	result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:warMachines"));
	result.description = "First Aid Tent creature healing increases by " + std::to_string(rule->tentPercentPerLevel)
		+ "% per hero level, up to +" + std::to_string(rule->tentMaximumPercent)
		+ "%. Battlefield Medic uses that enhanced budget under its ordinary rules. Repair and cleansing count are unchanged. ";
	const bool currentMagic = newHorizonsMagic::rulesActive(magic)
		&& magic["rulesetVersion"].Integer() == newHorizonsMagic::CURRENT_RULESET_VERSION;
	if(currentMagic)
		result.description += "The Spell Power-derived rate of Regeneration and legitimately acquired Mass Regeneration increases by "
			+ std::to_string(rule->regenerationPercentPerLevel) + "% per hero level, up to +"
			+ std::to_string(rule->regenerationMaximumPercent)
			+ "%. Their fixed 25%, Herbalist's 10 percentage points and the 50% total cap are unchanged. At zero Spell Power this adds nothing. ";
	else
		result.description += "Regeneration retains its ordinary effect under this game's legacy magic rules. ";
	result.description += "This grants no spell, Nature rank or Herbalist perk, and does not improve Cure.";
	if(capturedHero && currentMagic)
		result.description += " Current Regeneration SP-component increase: +"
			+ std::to_string(herbalSurgeonRegenerationPercent(capturedHero)) + "%.";
	return true;
}
}
