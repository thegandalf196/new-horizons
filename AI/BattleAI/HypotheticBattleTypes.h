/*
 * HypotheticBattleTypes.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <vstd/RNG.h>
#include <cstdint>
#include <memory>

#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/battle/BattleHex.h"

class StackWithBonuses;

/// Optional nested diagnostics, never additional disjoint exchange body time.
struct DLL_LINKAGE HypotheticLifecycleTiming
{
	struct DLL_LINKAGE StageTiming
	{
		uint64_t calls = 0;
		uint64_t totalMicroseconds = 0;
		uint64_t totalNanoseconds = 0;
		void accumulate(const StageTiming & sample)
		{
			calls += sample.calls;
			totalMicroseconds += sample.totalMicroseconds;
			totalNanoseconds += sample.totalNanoseconds;
		}
	};
	struct DLL_LINKAGE EffectWorkTiming
	{
		StageTiming stackedQuery;
		StageTiming rawQuery;
		/// Inclusive snapshot filtering, copies, alias reconstruction and overlay cleanup.
		StageTiming snapshotMaterialization;
		StageTiming selectiveRemovalQuery;
		/// Inclusive filtering, invalidation and control-state restoration.
		StageTiming removalMutation;
		/// Inclusive aging, expiry, invalidation and capacity/control restoration.
		StageTiming roundAging;
	};
	StageTiming activationExpiryRemoval;
	StageTiming activationGamblerRemoval;
	StageTiming roundTimedUpdates;
	// Finer disjoint work regions nested within the corresponding parents above.
	EffectWorkTiming activationExpiryWork;
	EffectWorkTiming activationGamblerWork;
	EffectWorkTiming roundTimedWork;
};

/// Detached projection result captured before one-strike Luck modifiers expire.
/// UNKNOWN preserves probabilistic damage without inventing a sampled outcome.
enum class ProjectedLuckOutcome : uint8_t
{
	UNKNOWN,
	POSITIVE,
	NEGATIVE,
	NEUTRAL
};

///Fake random generator, used by AI to evaluate random server behavior
class RNGStub final : public vstd::RNG
{
public:
	int nextInt() override
	{
		return 0;
	}

	int nextBinomialInt(int coinsCount, double coinChance) override
	{
		return coinsCount * coinChance;
	}

	int nextInt(int lower, int upper) override
	{
		return (lower + upper) / 2;
	}
	int64_t nextInt64(int64_t lower, int64_t upper) override
	{
		return (lower + upper) / 2;
	}
	double nextDouble(double lower, double upper) override
	{
		return (lower + upper) / 2;
	}

	int nextInt(int upper) override
	{
		return upper / 2;
	}
	int64_t nextInt64(int64_t upper) override
	{
		return upper / 2;
	}
	double nextDouble(double upper) override
	{
		return upper / 2;
	}
};

struct ProjectedPrimalBurstHit
{
	std::shared_ptr<StackWithBonuses> preHitTarget;
	PlayerColor targetController;
	int64_t actualDamage = 0;
};

struct ProjectedPlagueHit
{
	std::shared_ptr<const StackWithBonuses> preHitTarget;
	PlayerColor targetController;
	int64_t finalizedDamage = 0;
};

struct ProjectedOverwatchHit
{
	uint32_t shooterId = 0;
	BattleHex position;
	int64_t healthLoss = 0;
};
