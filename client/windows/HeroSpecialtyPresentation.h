/* Part of VCMI / New Horizons; GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

struct HeroSpecialtyPresentation
{
	std::string name;
	std::string description;
	std::optional<CreatureID> creature;
	size_t prototypeFrame = 0;
	AnimationPath animation(bool small = false) const
	{
		return AnimationPath::builtin(creature ? "CPRSMALL" : small ? "UN32" : "UN44");
	}
	size_t frame() const
	{
		return creature ? creature->toCreature()->getIconIndex() : prototypeFrame;
	}
};

// Explicit read-only context: an absent/captured legacy target never consults
// installed defaults. The original prototype remains the compatibility alias.
inline HeroSpecialtyPresentation heroSpecialtyPresentation(const CHero & hero,
	const JsonNode & development, std::optional<CreatureID> target)
{
	HeroSpecialtyPresentation result{hero.getSpecialtyNameTranslated(),
		hero.getSpecialtyDescriptionTranslated(), std::nullopt, static_cast<size_t>(hero.imageIndex)};
	if(!target || !hero.creatureLineSpecialtyAlias || *target == hero.creatureLineSpecialtyAlias->creature)
		return result;
	const auto rules = newHorizonsHeroes::creatureLineSpecialtyRules(development);
	if(!rules)
		return result;
	result.creature = target;
	result.name = target->toCreature()->getNamePluralTranslated();
	result.description = result.name + " and their upgrades gain +" + std::to_string(rules->speed)
		+ " Speed and +" + std::to_string(rules->initiative) + " Initiative. They also gain +"
		+ std::to_string(rules->attributePerStep) + " Creature Attack and +"
		+ std::to_string(rules->attributePerStep) + " Creature Defense per "
		+ std::to_string(rules->levelStep) + " hero levels, up to +"
		+ std::to_string(rules->attributeMaximum) + " at level "
		+ std::to_string(rules->levelStep * rules->attributeMaximum) + ".";
	return result;
}

inline HeroSpecialtyPresentation heroSpecialtyPresentation(const CGHeroInstance & hero)
{
	auto result = heroSpecialtyPresentation(*hero.getHeroType(), hero.getPrimaryGrowthRules(),
		hero.getCreatureLineSpecialtyTarget());
	// Preserve all existing spell/skill-specialty and instance description logic.
	result.description = hero.getSpecialtyDescriptionTranslated();
	return result;
}
