/*
 * NewHorizonsHoldAnchor.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <cstdint>
#include <vector>

#include "../../lib/battle/BattleHex.h"
#include "../../lib/battle/BattleSide.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/Unit.h"

namespace newHorizonsHoldAnchor
{
struct Footprint
{
	uint32_t unitId = 0;
	BattleHex head = BattleHex::INVALID;
	BattleHex rear = BattleHex::INVALID;

	bool operator==(const Footprint &) const = default;
};

/// Only effective, issue-time anchors: late arrivals, moved stacks and stacks
/// controlled by the other player never acquire a visual benefit. Read each
/// frame rather than retaining UI state across expiry, death or control changes.
inline std::vector<Footprint> activeAnchors(const CBattleInfoCallback & battle)
{
	std::vector<Footprint> result;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto order = battle.battleGetHeroOrderState(side, HeroCommand::HOLD_THE_LINE);
		if(!order)
			continue;
		for(const auto & anchor : order->anchors)
		{
			const auto * unit = battle.battleGetUnitByID(anchor.unitId);
			if(!battle.battleOrderBenefitAppliesTo(*order, side, unit))
				continue;
			Footprint footprint;
			footprint.unitId = unit->unitId();
			footprint.head = unit->getPosition();
			footprint.rear = unit->doubleWide() ? unit->occupiedHex() : BattleHex::INVALID;
			if(footprint.head.isAvailable() && (!unit->doubleWide() || footprint.rear.isAvailable()))
				result.push_back(footprint);
		}
	}
	return result;
}
}
