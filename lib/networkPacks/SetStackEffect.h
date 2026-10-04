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
		const auto containsAbilitySuppression = [](const auto & effects)
		{
			return std::ranges::any_of(effects, [](const auto & stackEffects)
			{
				return std::ranges::any_of(stackEffects.second, [](const Bonus & bonus)
				{
					return bonus.type == BonusType::CREATURE_ABILITY_SUPPRESSION;
				});
			});
		};
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_CREATURE_ABILITY_SUPPRESSION)
			&& (containsAbilitySuppression(toAdd) || containsAbilitySuppression(toUpdate)
				|| containsAbilitySuppression(toRemove)))
			throw std::runtime_error("Cannot discard creature ability suppression in stack-effect packet");
		const auto containsStatusMetadata = [](const auto & effects)
		{
			return std::ranges::any_of(effects, [](const auto & stackEffects)
			{
				return std::ranges::any_of(stackEffects.second, [](const Bonus & bonus)
				{
					return bonus.hasStatusMetadata();
				});
			});
		};
		if(h.saving && !h.hasFeature(Handler::Version::BONUS_STATUS_TAGS)
			&& (containsStatusMetadata(toAdd) || containsStatusMetadata(toUpdate)
				|| containsStatusMetadata(toRemove)))
			throw std::runtime_error("Cannot discard bonus status metadata in stack-effect packet");
		const auto containsElementalDamage = [](const auto & effects)
		{
			return std::ranges::any_of(effects, [](const auto & stackEffects)
			{
				return std::ranges::any_of(stackEffects.second, [](const Bonus & bonus)
				{
					return bonus.type == BonusType::ELEMENTAL_SPELL_DAMAGE;
				});
			});
		};
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_ELEMENTAL_SPELL_DAMAGE)
			&& (containsElementalDamage(toAdd) || containsElementalDamage(toUpdate)
				|| containsElementalDamage(toRemove)))
			throw std::runtime_error("Cannot discard elemental spell damage in stack-effect packet");
		h & battleID;
		h & toAdd;
		h & toUpdate;
		h & toRemove;
		assert(battleID != BattleID::NONE);
	}
};
