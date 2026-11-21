/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#pragma once

#include <cstdint>

class CGHeroInstance;
class CGTownInstance;

namespace newHorizonsEstates
{
/// Steward income under core Stewardship (Estates policy 2) without the perk.
inline constexpr int32_t CORE_STEWARD_GOLD = 500;
/// Steward income with the Stewardship perk, and for every policy 1 steward.
inline constexpr int32_t STEWARDSHIP_PERK_GOLD = 1000;

/// Reads the kingdom's single assignment map and revalidates current ownership
/// and qualification. Physical residence is deliberately irrelevant.
DLL_LINKAGE const CGHeroInstance * townSteward(const CGTownInstance & town);
/// Policy 2 admits every owned hero; policy 1 requires the Stewardship perk.
/// Callers still require the hero to be one of the owner's current map heroes.
DLL_LINKAGE bool eligibleSteward(const CGHeroInstance * hero);
/// Daily Gold this hero adds to the town it stewards, before income handicap.
DLL_LINKAGE int32_t stewardGold(const CGHeroInstance & hero);
/// The owned town this hero currently and validly stewards, if any.
DLL_LINKAGE const CGTownInstance * stewardedTown(const CGHeroInstance & hero);
}
