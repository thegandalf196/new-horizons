/*
 * NewHorizonsGlyphsOfFear.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsGlyphsOfFear.h"
#include "CGHeroInstance.h"
#include "CGTownInstance.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../callback/IGameInfoCallback.h"
#include "../gameState/CGameState.h"
#include "../mapping/CMap.h"

namespace newHorizonsGlyphsOfFear
{
TConstBonusListPtr moraleBonuses(const CGHeroInstance * hero, std::optional<int3> location)
{
	auto result = std::make_shared<BonusList>();
	if(!hero || !hero->cb || !hero->tempOwner.isValidPlayer())
		return result;
	const auto aura = newHorizonsHeroes::capabilityGlyphsOfFearAura(hero->getCapabilityRules());
	if(!aura)
		return result;
	const auto at = location.value_or(hero->visitablePos());
	const auto & map = hero->cb->gameState().getMap();
	if(!map.isInTheMap(at))
		return result;
	for(const auto id : map.getAllTowns())
	{
		const auto * town = hero->cb->getTown(id);
		if(!town || town->getFactionID() != FactionID::FORTRESS
			|| !town->hasBuilt(BuildingID::SPECIAL_3)
			|| hero->cb->getPlayerRelations(hero->tempOwner, town->tempOwner) != PlayerRelations::ENEMIES)
			continue;
		const auto center = town->visitablePos();
		if(center.z != at.z)
			continue;
		// Map positions are validated above. A per-axis bound also makes the
		// squared sum safe even for the largest configured int32 radius.
		const int64_t dx = static_cast<int64_t>(center.x) - at.x;
		const int64_t dy = static_cast<int64_t>(center.y) - at.y;
		const int64_t radius = aura->radius;
		if(std::abs(dx) > radius || std::abs(dy) > radius
			|| dx * dx > radius * radius - dy * dy)
			continue;
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
			BonusSource::TOWN_STRUCTURE, aura->morale, BonusSourceID(town->id));
		bonus->appliedByEnemy = true;
		bonus->bonusOwner = town->tempOwner;
		bonus->description.appendRawString("Glyphs of Fear");
		result->push_back(bonus);
	}
	return result;
}

TConstBonusListPtr appendMoraleBonuses(TConstBonusListPtr original,
	const CGHeroInstance * hero, std::optional<int3> location)
{
	const auto aura = moraleBonuses(hero, location);
	if(aura->empty())
		return original;
	auto result = std::make_shared<BonusList>();
	for(const auto & bonus : *original)
		result->push_back(bonus);
	for(const auto & bonus : *aura)
		result->push_back(bonus);
	return result;
}
}
