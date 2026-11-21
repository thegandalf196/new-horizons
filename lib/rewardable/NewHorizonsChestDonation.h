/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../constants/EntityIdentifiers.h"

class CGHeroInstance;
class IGameEventCallback;

namespace Rewardable
{
struct Configuration;
}

namespace newHorizonsRewardable
{
/// Gold of the option a hero declines by taking the Experience of a treasure chest,
/// measured after Tax Collector; 0 when the chosen reward is not that Experience.
DLL_LINKAGE int32_t declinedChestGold(const Rewardable::Configuration & configuration, ui32 chosen,
	const CGHeroInstance & hero, MapObjectID object);
/// Caitlin's Alms of the Faithful donation for that choice; 0 for anyone else.
DLL_LINKAGE int32_t chestDonation(const Rewardable::Configuration & configuration, ui32 chosen,
	const CGHeroInstance & hero, MapObjectID object);
DLL_LINKAGE void grantChestDonation(IGameEventCallback & events, const Rewardable::Configuration & configuration,
	ui32 chosen, const CGHeroInstance & hero, MapObjectID object);
}
