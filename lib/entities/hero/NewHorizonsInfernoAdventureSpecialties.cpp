/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsInfernoAdventureSpecialties.h"
#include "NewHorizonsInfernoSpecialties.h"
#include "../../mapObjects/CGHeroInstance.h"
#include <limits>

namespace newHorizonsHeroes
{

TResource cutpurseGold(const CutpurseRules & rules, int level, TResource treasury, TResource recipientGold)
{
	if(treasury <= 0)
		return 0;
	const int64_t basisPoints = std::min<int64_t>(int64_t{rules.maximumPercent} * 100,
		int64_t{rules.basePercent} * 100 + int64_t{rules.percentPerLevelBasisPoints} * std::max(0, level - 1));
	const int64_t amount = int64_t{treasury} * basisPoints / 10000;
	const int64_t headroom = GameConstants::PLAYER_RESOURCES_CAP - recipientGold;
	return static_cast<TResource>(std::max<int64_t>(0, std::min({amount, headroom, int64_t{treasury}})));
}

int sulfurSenseAmount(const SulfurSenseRules & rules, int level)
{
	return rules.baseSulfur + std::max(1, level) / rules.levelsPerAdditionalSulfur;
}

TResource cutpurseGold(const CGHeroInstance * hero, TResource treasury, TResource recipientGold)
{
	const auto * rules = infernoRules(hero, "core:octavia");
	return rules && rules->cutpurse ? cutpurseGold(*rules->cutpurse, hero->level, treasury, recipientGold) : 0;
}

int sulfurSenseAmount(const CGHeroInstance * hero)
{
	const auto * rules = infernoRules(hero, "core:calid");
	return rules && rules->sulfurSense ? sulfurSenseAmount(*rules->sulfurSense, hero->level) : 0;
}

ResourceSet sulfurSensePickup(const ResourceSet & reward, const CGHeroInstance * hero)
{
	ResourceSet result = reward;
	const auto sulfur = result[EGameResID::SULFUR];
	const int64_t headroom = std::max<int64_t>(0, GameConstants::PLAYER_RESOURCES_CAP - sulfur);
	result[EGameResID::SULFUR] += static_cast<TResource>(std::min<int64_t>(sulfurSenseAmount(hero), headroom));
	return result;
}
}
