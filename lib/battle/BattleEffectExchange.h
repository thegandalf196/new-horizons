/*
 * BattleEffectExchange.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../bonuses/Bonus.h"
#include "../json/JsonNode.h"
#include <array>

namespace battle
{
class CUnitState;

/// Only state owned by transferable spell effects. Confusion history, creature
/// HP/casualties, Orders and intrinsic abilities are deliberately not transferred.
struct DLL_LINKAGE BattleEffectSidecars
{
	int32_t regenerationRateMillionths = 0;
	int64_t regenerationPendingMicroHealth = 0;
	int64_t guardianSpiritHitPoints = 0;
	int32_t guardianSpiritRoundsRemaining = 0;
	int32_t capacityRegenerationRemainderTenths = 0;
	bool confusionPending = false;
	PlayerColor confusionCaster = PlayerColor::CANNOT_DETERMINE;
	bool confusionConfounder = false;
	bool operator==(const BattleEffectSidecars &) const = default;
	void validate() const;
	template<typename Handler> void serialize(Handler & h)
	{
		h & regenerationRateMillionths;
		h & regenerationPendingMicroHealth;
		h & guardianSpiritHitPoints;
		h & guardianSpiritRoundsRemaining;
		h & capacityRegenerationRemainderTenths;
		h & confusionPending;
		h & confusionCaster;
		h & confusionConfounder;
	}
};

struct DLL_LINKAGE BattleEffectSnapshot
{
	/// Complete, unstacked, source-local SPELL_EFFECT list, including duplicates
	/// and excluded effects retained verbatim by the caller's replacement plan.
	std::vector<Bonus> effects;
	BattleEffectSidecars sidecars;
	/// Read-only stale-state guard, never loaded or exchanged as replacement HP.
	JsonNode recipientHealth;
	int32_t capacityHealthReferenceMax = 0;
	template<typename Handler> void serialize(Handler & h)
	{
		h & effects;
		// Bonus's legacy wire DTO deliberately omits dynamic bonusOwner. This
		// exact transaction carries it explicitly without changing ordinary Bonus.
		std::vector<PlayerColor> owners;
		if(h.saving)
			for(const auto & effect : effects)
				owners.push_back(effect.bonusOwner);
		h & owners;
		if(!h.saving)
		{
			if(owners.size() != effects.size())
				throw std::runtime_error("Invalid exact spell-effect owner sidecar count");
			for(size_t index = 0; index < effects.size(); ++index)
				effects[index].bonusOwner = owners[index];
		}
		h & sidecars;
		h & recipientHealth;
		h & capacityHealthReferenceMax;
	}
};

struct DLL_LINKAGE BattleEffectExchangeEndpoint
{
	uint32_t id = 0;
	BattleEffectSnapshot expected;
	BattleEffectSnapshot replacement;
	template<typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & expected;
		h & replacement;
	}
};

struct DLL_LINKAGE BattleEffectExchange
{
	std::array<BattleEffectExchangeEndpoint, 2> endpoints;
	void validateShape() const;
	template<typename Handler> void serialize(Handler & h)
	{
		for(auto & endpoint : endpoints)
			h & endpoint;
	}
};

DLL_LINKAGE bool exactBattleEffectEqual(const Bonus & first, const Bonus & second);
DLL_LINKAGE bool exactBattleEffectsEqual(const std::vector<Bonus> & first, const std::vector<Bonus> & second);
DLL_LINKAGE bool exactBattleEffectSnapshotEqual(const BattleEffectSnapshot & first, const BattleEffectSnapshot & second);
DLL_LINKAGE BattleEffectSidecars captureBattleEffectSidecars(const CUnitState & unit);
DLL_LINKAGE void commitBattleEffectSidecars(CUnitState & unit, const BattleEffectSidecars & sidecars) noexcept;
struct DLL_LINKAGE PreparedBattleEffectHealth
{
	std::shared_ptr<const IBonusBearer> bonuses;
	std::shared_ptr<CUnitState> unit;
};
DLL_LINKAGE PreparedBattleEffectHealth prepareBattleEffectHealth(const CUnitState & unit,
	const BattleEffectSnapshot & expected, const BattleEffectSnapshot & replacement);
}
