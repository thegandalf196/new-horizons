/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/battle/DamageInteger.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/CRandomGenerator.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <cmath>
#include <limits>

namespace
{
nativeDamage::Facts facts(double damage, double count)
{
	nativeDamage::Facts result;
	result.attacker.minDamage = result.attacker.maxDamage = damage;
	result.attacker.count = count;
	result.defender.count = std::numeric_limits<int32_t>::max();
	result.defender.maxHealth = result.defender.firstHealth = std::numeric_limits<int32_t>::max();
	return result;
}
}

TEST(NativeDamageBoundary, LegalIntegerStackAndDamageWithMultiplierSaturateFinalIntegers)
{
	DamageAttackInfo info;
	info.heroOrderFinalDamageMultiplier = 1000;
	const auto input = facts(std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max());
	const auto result = nativeDamage::calculate(info, input);
	EXPECT_EQ(result.damage.min, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(result.damage.max, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(result.damageBeforeDefense.min, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(result.damageBeforeDefense.max, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(result.kills.min, std::numeric_limits<int32_t>::max());
	EXPECT_EQ(result.kills.max, std::numeric_limits<int32_t>::max());
}

TEST(NativeDamageBoundary, LargestRepresentableValueBelowInt64LimitRemainsExact)
{
	const double below = std::nextafter(static_cast<double>(std::numeric_limits<int64_t>::max()), 0.0);
	const auto result = nativeDamage::calculate(DamageAttackInfo{}, facts(below, 1));
	EXPECT_EQ(result.damage.min, static_cast<int64_t>(below));
	EXPECT_EQ(result.damage.max, static_cast<int64_t>(below));
}

TEST(NativeDamageBoundary, OrdinaryFloorMinimumDamageAndCasualtiesRemainUnchanged)
{
	auto input = facts(20.75, 3);
	input.defender.firstHealth = 3;
	input.defender.maxHealth = 10;
	const auto result = nativeDamage::calculate(DamageAttackInfo{}, input);
	EXPECT_EQ(result.damage.min, 62);
	EXPECT_EQ(result.damage.max, 62);
	EXPECT_EQ(result.damageBeforeDefense.min, 62);
	EXPECT_EQ(result.kills.min, 6);
	EXPECT_EQ(result.kills.max, 6);
	DamageAttackInfo zero;
	zero.heroOrderFinalDamageMultiplier = 0;
	EXPECT_EQ(nativeDamage::calculate(zero, input).damage.min, 1);
}

TEST(NativeDamageBoundary, LuckWeightedDoubleRoundingSaturatesWithoutChangingOrdinaryFloor)
{
	const auto maximum = std::numeric_limits<int64_t>::max();
	const double weighted = maximum * 0.75 + maximum * 0.25;
	EXPECT_EQ(damageInteger::boundedNonnegative(weighted), maximum);
	EXPECT_EQ(damageInteger::boundedNonnegative(11 * 0.75 + 24 * 0.25), 14);
}

TEST(NativeDamageBoundary, SamplingMatchesWideMeanAndRetainsExactRandomDrawSequence)
{
	BattleInfo battle(nullptr);
	const auto maximum = std::numeric_limits<int64_t>::max();
	for(const auto range : {DamageRange{1, 100}, DamageRange{maximum - 1000, maximum}})
		for(int count = 1; count <= 12; ++count)
		{
			CRandomGenerator actual(719), control(719);
			boost::multiprecision::uint128_t sum = 0;
			const int samples = std::min(10, count);
			for(int draw = 0; draw < samples; ++draw) sum += control.nextInt64(range.min, range.max);
			EXPECT_EQ(battle.getActualDamage(range, count, actual), (sum / samples).convert_to<int64_t>());
			EXPECT_EQ(actual.nextInt(), control.nextInt());
		}
	CRandomGenerator actual(719), control(719);
	EXPECT_EQ(battle.getActualDamage({maximum, maximum}, 10, actual), maximum);
	EXPECT_EQ(actual.nextInt(), control.nextInt());
	EXPECT_THROW(battle.getActualDamage({1, 100}, 0, actual), std::invalid_argument);
	EXPECT_THROW(battle.getActualDamage({-1, 100}, 1, actual), std::invalid_argument);
}
