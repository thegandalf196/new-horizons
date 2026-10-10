/*
 * NewHorizonsLighthouse.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#pragma once

#include "../bonuses/Bonus.h"
#include "../constants/EntityIdentifiers.h"
#include "../int3.h"
#include "../json/JsonNode.h"
#include <optional>

class CGHeroInstance;
class CGTownInstance;
class TurnInfo;

namespace newHorizonsLighthouse
{
constexpr int SEA_MOVEMENT_PERCENT = 20;
constexpr auto STACKING_KEY = "new-horizons:castle.lighthouseDeparture";

struct DLL_LINKAGE DepartureReceipt
{
	ObjectInstanceID town = ObjectInstanceID::NONE;
	int32_t day = -1;

	void validate() const;
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validate();
		h & town;
		h & day;
		if(!h.saving)
			validate();
	}
};

/// Exact owned Castle port classification. No player-wide or adventure Lighthouse effect.
DLL_LINKAGE const CGTownInstance * departureTown(const CGHeroInstance & hero,
	const int3 & destination, bool requireBoat = true);
DLL_LINKAGE int departurePercent(const CGHeroInstance & hero);
DLL_LINKAGE void validateRulesSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE bool hasDepartureBonus(const CGHeroInstance & hero, int turn = 0);
DLL_LINKAGE bool isLegacyCastleBonus(const Bonus & bonus);
DLL_LINKAGE Bonus departureBonus(const CGTownInstance & town, const CGHeroInstance & hero);
DLL_LINKAGE int movementAfterDeparture(int remaining, int stepCost, const TurnInfo & info);
}
