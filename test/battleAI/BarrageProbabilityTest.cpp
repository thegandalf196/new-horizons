/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../AI/BattleAI/BarrageProbability.h"
#include "../../lib/battle/IBattleInfoCallback.h"
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace
{
// Independent tiny oracle: enumerate native per-shot draws and floor EACH
// average. No inclusion/exclusion or production probability helper is reused.
long double enumerate(const DamageRange & damage, int count, int shots,
	const barrageProbability::TargetWindow & target)
{
	const int samples = std::min(10, count);
	uint64_t accepted = 0, outcomes = 0;
	std::function<void(int, int64_t)> shot;
	shot = [&](int remaining, int64_t total)
	{
		if(remaining == 0)
		{
			++outcomes;
			const auto shield = target.arcaneShield + target.guardianSpirit;
			accepted += total >= target.firstCreatureHP + target.temporaryHitPoints + shield
				&& total < target.totalHealth + shield;
			return;
		}
		std::function<void(int, int64_t)> draw = [&](int left, int64_t sum)
		{
			if(left == 0) { shot(remaining - 1, total + sum / samples); return; }
			for(int64_t value = damage.min; value <= damage.max; ++value)
				draw(left - 1, sum + value);
		};
		draw(samples, 0);
	};
	shot(shots, 0);
	return static_cast<long double>(accepted) / outcomes;
}
}

TEST(BarrageProbabilityTest, TinyNativeAveragesNeverExceedExactCasualtyAndSurvivalChance)
{
	for(int low = 0; low <= 1; ++low)
		for(int width = 1; width <= 2; ++width)
			for(int count = 1; count <= 3; ++count)
				for(int shots = 1; shots <= 2; ++shots)
					for(int first = 1; first <= 5; ++first)
						for(int total = first + 1; total <= first + 3; ++total)
						{
							const DamageRange range{low, low + width};
							const barrageProbability::TargetWindow target{first, total};
							const auto actual = barrageProbability::primaryCasualtyAndSurvival(range, count, shots, target);
							const auto exact = enumerate(range, count, shots, target);
							EXPECT_GE(actual, 0);
							EXPECT_LE(actual, exact + 1e-15L);
							if(shots == 1 || count == 1) EXPECT_NEAR(actual, exact, 1e-15L);
							EXPECT_LE(actual, 1);
							}
}

TEST(BarrageProbabilityTest, FirstBodyTemporaryHealthAndPhysicalAbsorptionUseKillThreshold)
{
	const DamageRange range{0, 3};
	const barrageProbability::TargetWindow target{1, 10, 2, 1, 2};
	EXPECT_NEAR(barrageProbability::primaryCasualtyAndSurvival(range, 1, 2, target),
		enumerate(range, 1, 2, target), 1e-15L);
	// Two native shots can only reach the first body at the joint maximum6.
	EXPECT_NEAR(barrageProbability::primaryCasualtyAndSurvival(range, 1, 2, target), 1.0L / 16, 1e-15L);
	EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({100, 200}, 10, 2, {1, 1}), 0);
}

TEST(BarrageProbabilityTest, ConstantAndGuaranteedBoundsRespectAllShots)
{
	EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({2, 2}, 100, 2, {3, 10}), 1);
	EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({2, 2}, 100, 2, {5, 10}), 0);
	EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({2, 2}, 100, 2, {3, 4}), 0);
	EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({2, 3}, 100, 3, {5, 10}), 1);
	EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({0, 3}, 100, 3, {1, 8}), 0);
	const auto prefix = barrageProbability::primaryCasualtyAndSurvival({0, 3}, 2, 2, {2, 100});
	EXPECT_GT(prefix, 0);
	EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({0, 3}, 2, 3, {2, 100}), prefix);
}

TEST(BarrageProbabilityTest, ExtremeRangesAndShieldsStayFiniteWithoutInt64IntermediateOverflow)
{
	constexpr auto maximum = std::numeric_limits<int64_t>::max();
	const auto broad = barrageProbability::primaryCasualtyAndSurvival({0, maximum},
		std::numeric_limits<int32_t>::max(), 2, {maximum / 2, maximum});
	EXPECT_TRUE(std::isfinite(broad));
	EXPECT_GT(broad, 0);
	EXPECT_LT(broad, 1);
	const auto shielded = barrageProbability::primaryCasualtyAndSurvival({maximum - 1, maximum},
		10, 2, {1, maximum, 0, maximum, maximum});
	EXPECT_EQ(shielded, 0);
	EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({maximum, maximum},
		10, std::numeric_limits<int>::max(), {1, maximum}), 0);
}

TEST(BarrageProbabilityTest, InactiveOrMalformedInputsCannotCreateAProcChance)
{
	for(int count : {-1, 0})
		EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({1, 2}, count, 2, {1, 10}), 0);
	for(int shots : {-1, 0})
		EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({1, 2}, 10, shots, {1, 10}), 0);
	EXPECT_EQ(barrageProbability::primaryCasualtyAndSurvival({1, 2}, 10, 2, {1, 0}), 0);
	EXPECT_THROW(barrageProbability::primaryCasualtyAndSurvival({2, 1}, 10, 2, {1, 10}), std::invalid_argument);
	EXPECT_THROW(barrageProbability::primaryCasualtyAndSurvival({-1, 1}, 10, 2, {1, 10}), std::invalid_argument);
	EXPECT_THROW(barrageProbability::primaryCasualtyAndSurvival({1, 2}, 10, 2, {1, 10, 0, -1}), std::invalid_argument);
}
