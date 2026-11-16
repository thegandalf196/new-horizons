/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../ResourceSet.h"
#include <cstddef>
#include <cstdint>

class CGHeroInstance;
class PlayerState;
class JsonNode;
class MetaString;

namespace newHorizonsEstates
{
/// Absence preserves the complete historical Estates catalogue and effects.
DLL_LINKAGE bool usesRevisedPolicy(const JsonNode & rules);
DLL_LINKAGE void validateRevisedPolicy(const JsonNode & rules);
DLL_LINKAGE void validateRevisedSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE bool hasRevisedPerk(const CGHeroInstance * hero, const char * perk);
DLL_LINKAGE bool anyRevisedHolder(const PlayerState & player, const char * perk);
DLL_LINKAGE ResourceSet visitReward(const ResourceSet & base, const CGHeroInstance * hero);
DLL_LINKAGE void appendVisitRewardBonus(MetaString & text, const ResourceSet & base,
	const ResourceSet & effective);
/// Add to ordinary production before independent bonuses and handicap stages.
DLL_LINKAGE uint32_t mineBaseProduction(uint32_t base, GameResID resource, const PlayerState * owner);
DLL_LINKAGE int marketplaceBonus(const PlayerState & player);
DLL_LINKAGE size_t capitolLimit(const PlayerState & player);
/// Caller uses the ordinary weekly income event; no separate initial-day grant.
DLL_LINKAGE ResourceSet weeklyAlchemistIncome(const PlayerState & player);
}
