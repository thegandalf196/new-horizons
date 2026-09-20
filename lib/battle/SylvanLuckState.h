/*
 * SylvanLuckState.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <set>
#include <cstdint>
#include <stdexcept>
#include <vector>

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

	bool active() const { return serendipity || naturesProvidence || fortunateAim; }
	int chanceLuck(int baseLuck, uint32_t unitId, bool focusFireShot) const
	{
		return baseLuck + (serendipity && !positiveLuckUnits.contains(unitId) ? 1 : 0)
			+ (fortunateAim && focusFireShot ? 1 : 0);
	}
	/// Returns whether a rolled bad-luck result is suppressed. Positive and
	/// negative outcomes are mutually exclusive and applied once per strike.
	bool recordStrike(uint32_t unitId, bool positive, bool negative)
	{
		if(!active())
			return false;
		if(positive)
			positiveLuckUnits.insert(unitId);
		if(negative && naturesProvidence && !negativeLuckIgnored)
		{
			negativeLuckIgnored = true;
			return true;
		}
		return false;
	}
	void nextRound() { negativeLuckIgnored = false; }
	bool operator==(const SylvanLuckState &) const = default;

	template <typename Handler> void serialize(Handler & h)
	{
		h & serendipity;
		h & naturesProvidence;
		h & fortunateAim;
		h & negativeLuckIgnored;
		h & positiveLuckUnits;
		if(!h.saving && ((!active() && !positiveLuckUnits.empty())
			|| (negativeLuckIgnored && !naturesProvidence)))
			throw std::runtime_error("Invalid Sylvan Luck battle state");
	}
};
