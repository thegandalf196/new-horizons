/*
 * SylvanLuckState.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <set>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "../serializer/ESerializationVersion.h"

/// Saved battle chance curve. A zero dice size is the legacy/global fallback.
struct DLL_LINKAGE LuckRollRules
{
	std::vector<int> goodChance;
	std::vector<int> badChance;
	int diceSize = 0;
	bool affectsAllTargets = false;
	bool operator==(const LuckRollRules &) const = default;
	template <typename Handler> void serialize(Handler & h)
	{
		h & goodChance;
		h & badChance;
		h & diceSize;
		h & affectsAllTargets;
		if(!h.saving && diceSize < 0)
			throw std::runtime_error("Invalid Luck chance curve");
	}
};

/// Battle-start snapshot: old battles default to an inert state. Only the
/// authoritative strike roll changes history; damage targets never roll luck.
struct DLL_LINKAGE SylvanLuckState
{
	bool serendipity = false;
	bool naturesProvidence = false;
	bool fortunateAim = false;
	bool negativeLuckIgnored = false;
	std::set<uint32_t> positiveLuckUnits;
	bool forestsFavor = false;
	bool luckyRecovery = false;
	bool sharedFortune = false;
	bool cascadingFortune = false;
	std::set<uint32_t> speedUnits;
	std::set<uint32_t> sharedUnits;
	std::set<uint32_t> cascadingUnits;
	bool cascadingPending = false;

	bool active() const { return serendipity || naturesProvidence || fortunateAim || extendedActive(); }
	bool extendedActive() const { return forestsFavor || luckyRecovery || sharedFortune || cascadingFortune; }
	int speedBonus(uint32_t unitId) const { return speedUnits.contains(unitId) ? 2 : 0; }
	int temporaryLuck(uint32_t unitId) const
	{
		return (sharedUnits.contains(unitId) ? 1 : 0) + (cascadingUnits.contains(unitId) ? 3 : 0);
	}
	int chanceLuck(int baseLuck, uint32_t unitId, bool focusFireShot) const
	{
		return baseLuck + (serendipity && !positiveLuckUnits.contains(unitId) ? 1 : 0)
			+ (fortunateAim && focusFireShot ? 1 : 0) + temporaryLuck(unitId);
	}
	/// Returns whether a rolled bad-luck result is suppressed. Positive and
	/// negative outcomes are mutually exclusive and applied once per strike.
	bool recordStrike(uint32_t unitId, bool positive, bool negative)
	{
		if(!active())
			return false;
		if(positive && positiveLuckUnits.insert(unitId).second && forestsFavor)
			speedUnits.insert(unitId);
		if(negative && naturesProvidence && !negativeLuckIgnored)
		{
			negativeLuckIgnored = true;
			return true;
		}
		return false;
	}
	/// Called once after all victims of a positive strike have been resolved.
	/// Adjacency and enemy-kill classification are supplied by the battle query.
	void finishPositiveStrike(const std::vector<uint32_t> & adjacentFriends, bool enemyStackKilled)
	{
		if(sharedFortune)
			sharedUnits.insert(adjacentFriends.begin(), adjacentFriends.end());
		if(cascadingFortune && enemyStackKilled)
			cascadingPending = true;
	}
	static int64_t recoveryAmount(int64_t actualDamage) { return std::max<int64_t>(0, actualDamage) / 10; }
	void endActivation()
	{
		speedUnits.clear();
		cascadingUnits.clear();
	}
	void beginActivation(uint32_t unitId, bool friendly)
	{
		endActivation();
		// Expire a recipient's gift even if control changed in the meantime.
		sharedUnits.erase(unitId);
		if(friendly && cascadingPending)
		{
			cascadingUnits.insert(unitId);
			cascadingPending = false;
		}
	}
	void nextRound()
	{
		negativeLuckIgnored = false;
		endActivation();
	}
	bool operator==(const SylvanLuckState &) const = default;

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && !h.hasFeature(ESerializationVersion::NEW_HORIZONS_SYLVAN_FORTUNE_EFFECTS) && extendedActive())
			throw std::runtime_error("Cannot downgrade extended Sylvan Luck state");
		h & serendipity;
		h & naturesProvidence;
		h & fortunateAim;
		h & negativeLuckIgnored;
		h & positiveLuckUnits;
		if(h.hasFeature(ESerializationVersion::NEW_HORIZONS_SYLVAN_FORTUNE_EFFECTS))
		{
			h & forestsFavor;
			h & luckyRecovery;
			h & sharedFortune;
			h & cascadingFortune;
			h & speedUnits;
			h & sharedUnits;
			h & cascadingUnits;
			h & cascadingPending;
		}
		else if(!h.saving)
		{
			forestsFavor = luckyRecovery = sharedFortune = cascadingFortune = cascadingPending = false;
			speedUnits.clear();
			sharedUnits.clear();
			cascadingUnits.clear();
		}
		if(!h.saving && ((!forestsFavor && !speedUnits.empty()) || (!sharedFortune && !sharedUnits.empty())
			|| (!cascadingFortune && (cascadingPending || !cascadingUnits.empty())) || cascadingUnits.size() > 1))
			throw std::runtime_error("Invalid Sylvan Luck activation state");
		if(!h.saving && std::any_of(speedUnits.begin(), speedUnits.end(), [this](uint32_t id) { return !positiveLuckUnits.contains(id); }))
			throw std::runtime_error("Sylvan Luck speed gift without a positive trigger");
		if(!h.saving && ((!active() && !positiveLuckUnits.empty())
			|| (negativeLuckIgnored && !naturesProvidence)))
			throw std::runtime_error("Invalid Sylvan Luck battle state");
	}
};
