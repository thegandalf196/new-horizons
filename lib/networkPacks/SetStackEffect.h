/*
 * SetStackEffect.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <algorithm>

#include "NetPacksBase.h"

#include "../bonuses/Bonus.h"
#include "../battle/NewHorizonsOffense.h"

class IBattleState;

struct DLL_LINKAGE SetStackEffect : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	std::vector<std::pair<ui32, std::vector<Bonus>>> toAdd;
	std::vector<std::pair<ui32, std::vector<Bonus>>> toUpdate;
	std::vector<std::pair<ui32, std::vector<Bonus>>> toRemove;

	void visitTyped(ICPackVisitor & visitor) override;

	template <typename Handler> void serialize(Handler & h)
	{
		const auto containsNoQuarter = [](const auto & effects)
		{
			return std::ranges::any_of(effects, [](const auto & stackEffects)
			{
				return std::ranges::any_of(stackEffects.second, [](const Bonus & bonus)
				{
					return newHorizonsOffense::isNoQuarterBonus(&bonus);
				});
			});
		};
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_NO_QUARTER)
			&& (containsNoQuarter(toAdd) || containsNoQuarter(toUpdate) || containsNoQuarter(toRemove)))
			throw std::runtime_error("Cannot discard No Quarter stack effect");
		const auto containsPuppetMasterState = [](const auto & effects)
		{
			return std::ranges::any_of(effects, [](const auto & stackEffects)
			{
				return std::ranges::any_of(stackEffects.second, [](const Bonus & bonus)
				{
					return bonus.type == BonusType::PUPPET_MASTER_CONTROL || bonus.type == BonusType::LUCIDITY;
				});
			});
		};
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_PUPPET_MASTER_CONTROL)
			&& (containsPuppetMasterState(toAdd) || containsPuppetMasterState(toUpdate)
				|| containsPuppetMasterState(toRemove)))
			throw std::runtime_error("Cannot discard New Horizons Puppet Master stack effect");
		h & battleID;
		h & toAdd;
		h & toUpdate;
		h & toRemove;
		assert(battleID != BattleID::NONE);
	}
};
