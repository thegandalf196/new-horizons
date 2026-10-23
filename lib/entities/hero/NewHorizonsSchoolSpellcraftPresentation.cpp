/*
 * NewHorizonsSchoolSpellcraftPresentation.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../spells/NewHorizonsMagicInternal.h"
#include <array>

namespace newHorizonsHeroes
{
bool resolveSchoolSpellcraftPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	constexpr std::array<std::string_view, 4> identities{"core:zydar", "core:sandro", "core:gird", "core:styg"};
	constexpr std::array<std::string_view, 4> names{"Overcaster", "Dark Savant", "Primal Formula", "Mire Hexer"};
	constexpr std::array<std::string_view, 4> artwork{"ZydarOvercaster", "SandroDarkSavant", "GirdPrimalFormula", "StygMireHexer"};
	constexpr std::array<std::string_view, 4> schools{"Chaos or Havoc", "Shadow or Sorcery", "Chaos or Nature", "Nature or Shadow"};
	const auto found = std::ranges::find(identities, hero.getJsonKey());
	if(found == identities.end())
		return false;
	const auto index = found - identities.begin();
	const auto policy = schoolSpellcraftRules(development, static_cast<SchoolSpellcraftKind>(index));
	if(!policy)
		return false;
	result.artwork = "newHorizons/heroSpecialties/" + std::string(artwork[index]);
	result.name = names[index];
	result.description = "With Spellcraft learned, on " + std::string(schools[index]) + " spells, Spellcraft's Spell Power bonus increases by "
		+ std::to_string(policy->percentPerLevel) + "% per hero level, up to +"
		+ std::to_string(policy->maximumPercent) + "% of that bonus. ";
	result.description += index == 0
		? "Only damage increases, including ongoing and delayed damage. Healing, control and durations are unchanged. "
		: "This strengthens numerical effects already scaled by Spellcraft; fixed effects are unchanged. ";
	const bool providesBonus = newHorizonsMagic::spellcraftEfficiencyPercent(magic, MasteryLevel::EXPERT) > 100;
	if(!providesBonus)
		result.description += "Spellcraft provides no numerical bonus under this game's rules. ";
	result.description += "This does not grant Spellcraft. A spell matching both schools receives the bonus once.";
	if(capturedHero && providesBonus)
		result.description += " Current increase to the Spellcraft bonus: +"
			+ std::to_string(newHorizonsMagic::detail::registeredSpellcraftRank(capturedHero) > 0
				? cappedSpecialtyContribution(capturedHero->level, policy->percentPerLevel, policy->maximumPercent) : 0) + "%.";
	return true;
}
}
