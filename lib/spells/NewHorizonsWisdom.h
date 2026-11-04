/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../constants/EntityIdentifiers.h"
#include "../battle/BattleSide.h"
#include <cstdint>
class CGHeroInstance;
class CGTownInstance;
class IBattleInfo;
class JsonNode;
class ResourceSet;
namespace newHorizonsWisdom
{
DLL_LINKAGE int32_t familiarityCost(int32_t ordinary, uint32_t priorCasts);
DLL_LINKAGE int32_t effectivenessBonusPercent(const CGHeroInstance * hero, const IBattleInfo & battle, BattleSide side, SpellID spell);
DLL_LINKAGE int32_t archmagePenetrationPercent(const CGHeroInstance * hero, const JsonNode & rules, SpellID spell);
DLL_LINKAGE ResourceSet adventureSpellPurchaseCost(const JsonNode & rules, SpellID spell, const CGTownInstance & town);
DLL_LINKAGE int32_t conservationRecovery(const CGHeroInstance & hero, int64_t spent);
DLL_LINKAGE int32_t fastingRecovery(const CGHeroInstance & hero, bool victorious, bool completedHeroCast);
}
