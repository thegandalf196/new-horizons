/*
 * NewHorizonsIndividualSpecialtyPresentation.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsNecromancyPolicy.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/NewHorizonsMagic.h"

namespace newHorizonsHeroes
{
namespace
{
std::string percentage(int basisPoints)
{
	const auto whole = std::to_string(basisPoints / 100);
	if(basisPoints % 100 == 0)
		return whole;
	auto fraction = std::to_string(100 + basisPoints % 100).substr(1);
	if(fraction.back() == '0')
		fraction.pop_back();
	return whole + "." + fraction;
}
}

bool resolveIndividualRiderPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	const auto policy = individualSpecialtyRules(development);
	if(!policy)
		return false;
	if(hero.getJsonKey() == "core:isra")
	{
		const auto & rule = policy->graveReaper;
		result.artwork = "newHorizons/heroSpecialties/IsraGraveReaper";
		result.name = "Grave Reaper";
		result.description = "With Necromancy learned, its base raising rate gains +"
			+ std::to_string(rule.percentagePointsPerLevel) + " percentage points per hero level, up to +"
			+ std::to_string(rule.maximumPercentagePoints) + " points. Bone Collector and Amplifiers add separately; "
			"the total still caps at 100%. ";
		if(capturedHero && !newHorizonsNecromancy::policy(capturedHero->getPerkState().rules))
			result.description += "Death Lord follows the enhanced rate; Grave Knowledge remains 20%. ";
		result.description += "This does not grant Necromancy or bypass army capacity.";
		if(capturedHero)
			result.description += " Current base-rate bonus: +"
				+ std::to_string(graveReaperPercentagePoints(capturedHero)) + " percentage points.";
		return true;
	}
	if(hero.getJsonKey() == "core:malekith")
	{
		const auto & rule = policy->warlocksFormula;
		result.artwork = "newHorizons/heroSpecialties/MalekithWarlocksFormula";
		result.name = "Warlock's Formula";
		const int expertEfficiency = newHorizonsMagic::spellcraftEfficiencyPercent(magic, MasteryLevel::EXPERT);
		result.description = "For direct magical damage, Spellcraft's Spell Power bonus increases by "
			+ std::to_string(rule.percentPerLevel) + "% per hero level, up to +"
			+ std::to_string(rule.maximumPercent) + "% of that bonus. ";
		if(expertEfficiency > 100)
			result.description += "At the cap, Expert Spellcraft's +"
				+ percentage((expertEfficiency - 100) * 100) + "% becomes +"
				+ percentage(strengthenedSpellcraftBasisPoints(expertEfficiency, rule.maximumPercent) - 10000) + "%. ";
		else
			result.description += "Spellcraft provides no damage bonus under this game's rules. ";
		result.description += "Fixed damage, ongoing or indirect damage, healing, summons, control and durations are unchanged. "
			"This does not grant Spellcraft. Life Drain healing still follows its resolved damage.";
		if(capturedHero && expertEfficiency > 100)
			result.description += " Current increase to the Spellcraft bonus: +"
				+ std::to_string(warlocksFormulaPercent(capturedHero)) + "%.";
		return true;
	}
	return false;
}
}
