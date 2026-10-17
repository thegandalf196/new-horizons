/*
 * BattleUnitRootOrigin.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedBattleUnitRoots.h"
#include "BattleUnitRootOrigin.h"
#include "BattleInfo.h"
#include "../bonuses/PreparedBonusGraphStorage.h"
#include <stdexcept>

battle::BattleUnitRootIdentity battle::PreparedBattleUnitRoots::captureIdentity(const CStack & original) noexcept
{
	return {&original, original.ID, original.typeID, original.baseAmount,
		original.owner, original.side, original.slot, original.base, original.initialPosition,
		original.battleStartMaximumAggregateHP, original.rebirthOriginalAggregateHP,
		original.battle, original.env, original.getQueryTerrain(), original.isHypothetic()};
}

std::unique_ptr<const battle::BattleUnitRootOrigin> battle::PreparedBattleUnitRoots::captureOrigin(BattleInfo & source)
{
	BattleUnitRootOrigin origin{source.stacks.data(), source.stacks.size(), source.stacks.capacity(), {},
		PreparedBonusGraphStorage::currentGeneration()};
	origin.occurrences.reserve(origin.size);
	for(const auto & stack : source.stacks)
	{
		if(!stack)
			throw std::invalid_argument("Prepared roots require nonnull physical occurrences");
		origin.occurrences.push_back(captureIdentity(*stack));
	}
	return std::make_unique<const BattleUnitRootOrigin>(std::move(origin));
}
