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
#include "../battle/BattleForm.h"
#include "../battle/BattleEffectExchange.h"

class IBattleState;

struct DLL_LINKAGE SetStackEffect : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	std::vector<std::pair<ui32, std::vector<Bonus>>> toAdd;
	std::vector<std::pair<ui32, std::vector<Bonus>>> toUpdate;
	std::vector<std::pair<ui32, std::vector<Bonus>>> toRemove;
	std::optional<battle::BattleEffectExchange> exchange;
	void validateExchange() const
	{
		if(exchange)
		{
			if(!toAdd.empty() || !toUpdate.empty() || !toRemove.empty())
				throw std::runtime_error("Atomic spell-effect exchange cannot mix ordinary effect changes");
			exchange->validateShape();
		}
	}

	void visitTyped(ICPackVisitor & visitor) override;
	void validateConfusionMarkers() const
	{
		for(const auto * changes : {&toAdd, &toUpdate, &toRemove})
		{
			for(const auto & entry : *changes)
			{
				for(const Bonus & bonus : entry.second)
					bonus.validateConfusionPendingMarker();
			}
		}
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
		{
			validateExchange();
			for(const auto * changes : {&toAdd, &toUpdate, &toRemove})
				for(const auto & entry : *changes)
					for(const Bonus & bonus : entry.second)
						bonus.validateSwiftRebirthSerialization(h);
			for(const auto * changes : {&toAdd, &toUpdate, &toRemove})
				for(const auto & entry : *changes)
					for(const Bonus & bonus : entry.second)
						bonus.validateFrozenSerialization(h);
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_REALITY_WARP_EXCHANGE))
				for(const auto * changes : {&toAdd, &toUpdate, &toRemove})
					for(const auto & entry : *changes)
						for(const auto & bonus : entry.second)
							if(std::find(bonus.statusTags.begin(), bonus.statusTags.end(), BonusStatusTag::NON_TRANSFERABLE)
								!= bonus.statusTags.end())
								throw std::runtime_error("Cannot discard non-transferable effect metadata in an older packet format");
			if(exchange && !h.hasFeature(Handler::Version::NEW_HORIZONS_REALITY_WARP_EXCHANGE))
				throw std::runtime_error("Cannot discard atomic spell-effect exchange in an older packet format");
			validateConfusionMarkers();
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_SAFE_BATTLE_FORMS))
				for(const auto * changes : {&toAdd, &toUpdate, &toRemove})
					for(const auto & entry : *changes)
						for(const Bonus & bonus : entry.second)
							if(battle::isPolymorphMarker(&bonus))
								throw std::runtime_error("Cannot discard Polymorph marker in an older stack-effect format");
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_CONFUSION_MARKER))
			{
				for(const auto * changes : {&toAdd, &toUpdate, &toRemove})
				{
					for(const auto & entry : *changes)
					{
						for(const Bonus & bonus : entry.second)
						{
							if(bonus.type == BonusType::CONFUSION_PENDING)
								throw std::runtime_error("Cannot discard Confusion pending stack effect");
						}
					}
				}
			}
		}
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
		const auto containsBonusType = [](const auto & effects, BonusType type)
		{
			return std::ranges::any_of(effects, [type](const auto & stackEffects)
			{
				return std::ranges::any_of(stackEffects.second, [type](const Bonus & bonus)
				{
					return bonus.type == type;
				});
			});
		};
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_ELEMENTAL_SPELL_DAMAGE)
			&& (containsBonusType(toAdd, BonusType::ELEMENTAL_SPELL_DAMAGE)
				|| containsBonusType(toUpdate, BonusType::ELEMENTAL_SPELL_DAMAGE)
				|| containsBonusType(toRemove, BonusType::ELEMENTAL_SPELL_DAMAGE)))
			throw std::runtime_error("Cannot discard elemental spell damage in stack-effect packet");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE)
			&& (containsBonusType(toAdd, BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED)
				|| containsBonusType(toUpdate, BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED)
				|| containsBonusType(toRemove, BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED)))
			throw std::runtime_error("Cannot discard incoming elemental spell damage in stack-effect packet");
		h & battleID;
		h & toAdd;
		h & toUpdate;
		h & toRemove;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_REALITY_WARP_EXCHANGE))
			h & exchange;
		else if(!h.saving)
			exchange.reset();
		if(!h.saving)
			validateExchange();
		if(!h.saving)
			validateConfusionMarkers();
		assert(battleID != BattleID::NONE);
	}
};
