/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsParalyzingGaze.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
void validateParalyzingGazeSerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("additionalSpecialties")
		|| !rules["additionalSpecialties"].isStruct()
		|| !rules["additionalSpecialties"].Struct().contains("paralyzingGaze"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Paralyzing Gaze requires its one-shot retaliation save capability");
	validateAdditionalSpecialtiesSerialization(rules, true);
}
std::optional<bool> paralyzingGazePolicy(const JsonNode & rules)
{
	if(!usesRules(rules) || !rules.Struct().contains("additionalSpecialties")
		|| !rules["additionalSpecialties"].isStruct()
		|| !rules["additionalSpecialties"].Struct().contains("paralyzingGaze"))
		return std::nullopt;
	validateParalyzingGazeSerialization(rules, true);
	return rules["additionalSpecialties"]["paralyzingGaze"]["enabled"].Bool();
}
bool hasParalyzingGaze(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID().hasValue()
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:ajit"))
		&& paralyzingGazePolicy(hero->getPrimaryGrowthRules()).has_value();
}
bool paralyzingGazeEnabled(const CGHeroInstance * hero)
{
	return hasParalyzingGaze(hero) && paralyzingGazePolicy(hero->getPrimaryGrowthRules()).value();
}
bool resolveParalyzingGazePresentation(const CHero & hero, const JsonNode & development,
	SpecialtyPresentation & result)
{
	if(hero.getJsonKey() == "core:ajit")
		if(const auto enabled = paralyzingGazePolicy(development))
		{
			result.name = "Paralyzing Gaze";
			if(*enabled)
				result.artwork = "newHorizons/heroSpecialties/AjitParalyzingGaze";
			result.creature = CreatureID(CreatureID::decode("core:beholder"));
			result.description = *enabled ? "Damaging primary ranged attacks by Beholders and Evil Eyes suppress the target’s next otherwise eligible ordinary retaliation. Repeated hits leave one pending cancellation. Defiant can ignore this physical effect. Cure and Dispel do not remove it."
				: "Paralyzing Gaze is disabled for this game.";
			return true;
		}
	return false;
}

}
