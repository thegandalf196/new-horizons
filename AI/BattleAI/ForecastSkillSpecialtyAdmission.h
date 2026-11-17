/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/entities/hero/NewHorizonsSkillSpecialtyMarker.h"
#include "../../lib/entities/hero/CHero.h"
#include <algorithm>
#include <typeinfo>
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace forecast
{
/// A visible canonical Hero's exact local authored tag cannot execute a combat
/// event. Equal foreign copies and unauthored keys do not establish its origin.
inline bool isOwnedSkillSpecialtyMarker(const Bonus & marker, const CGHeroInstance * hero)
{
	if(marker.type != BonusType::NONE || !hero || typeid(*hero) != typeid(CGHeroInstance))
		return false;
	const auto * type = hero->getHeroType();
	const auto * rules = hero->compiledDevelopment();
	if(!type || !rules || !rules->skillSpecialty)
		return false;
	const auto & local = hero->getExportedBonusList();
	if(std::ranges::none_of(local, [&](const auto & value) { return value.get() == &marker; }))
		return false;
	const auto matches = [&](SecondarySkill skill)
	{
		return std::ranges::find(rules->skillSpecialty->skills, skill) != rules->skillSpecialty->skills.end()
			&& newHorizonsHeroes::skillSpecialtyMarker::matches(marker, type->getId(), skill);
	};
	return (type->secondarySkillSpecialtyAlias && matches(type->secondarySkillSpecialtyAlias->skill))
		|| (type->navigationSpecialtyProducer && rules->usesNavigationStartReplacement()
			&& matches(SecondarySkill(SecondarySkill::LOGISTICS)));
}
}
