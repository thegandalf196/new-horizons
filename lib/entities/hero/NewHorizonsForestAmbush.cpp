/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsForestAmbush.h"
#include "CHero.h"
#include "../../battle/NewHorizonsForestAmbush.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
bool resolveForestAmbushPresentation(const CHero & hero, const JsonNode & rules, SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:ivor")
		return false;
	const auto enabled = forestAmbushPolicy(rules);
	if(!enabled)
		return false;
	result.name = "Forest Ambush";
	result.artwork = "newHorizons/heroSpecialties/IvorForestAmbush";
	result.creature = CreatureID(CreatureID::decode("core:woodElf"));
	result.description = *enabled
		? "Wood Elves and Grand Elves that Wait before receiving an attack this round become concealed until their delayed activation. Concealment prevents deliberate hostile attacks and single-target spells; area and indirect effects still apply. Incoming attacks and outgoing physical creature attacks reveal them. Their first primary physical ranged strike from concealment or in that delayed activation gains +"
			+ std::to_string(newHorizonsForestAmbush::FIRST_SHOT_LUCK) + " Luck within normal limits. The bonus expires when that activation ends."
		: "Forest Ambush is disabled for this game.";
	return true;
}

void validateForestAmbushSerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("additionalSpecialties")
		|| !rules["additionalSpecialties"].isStruct()
		|| !rules["additionalSpecialties"].Struct().contains("forestAmbush"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Forest Ambush requires its Forest Ambush save capability");
	validateAdditionalSpecialtiesSerialization(rules, true);
}
std::optional<bool> forestAmbushPolicy(const JsonNode & rules)
{
	if(!usesRules(rules) || !rules.Struct().contains("additionalSpecialties")
		|| !rules["additionalSpecialties"].isStruct()
		|| !rules["additionalSpecialties"].Struct().contains("forestAmbush"))
		return std::nullopt;
	validateForestAmbushSerialization(rules, true);
	return rules["additionalSpecialties"]["forestAmbush"]["enabled"].Bool();
}
bool hasForestAmbush(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID().hasValue()
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:ivor"))
		&& forestAmbushPolicy(hero->getPrimaryGrowthRules()).has_value();
}
bool forestAmbushEnabled(const CGHeroInstance * hero)
{
	return hasForestAmbush(hero) && forestAmbushPolicy(hero->getPrimaryGrowthRules()).value();
}
}
