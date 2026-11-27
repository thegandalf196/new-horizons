/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/combatScripts/NativeHellfireBatteryDamage.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/combatScripts/NativeDamageFactors.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include <limits>
#include <stdexcept>

namespace
{
double commonFactor(const DamageAttackInfo & info, const nativeDamage::Facts & facts)
{
	return nativeDamage::commonDamageFactor(nativeDamage::finalDamageFactors(info, facts),
		nativeDamage::foldDamageModifiers(info, facts, true));
}
}

TEST(HellfireBatteryDamage, SplitKeepsOneRoundedBudgetAtEveryShare)
{
	for(const int total : {1, 2, 3, 9, 51, 100, 101})
		for(const int share : {0, 200, 500, 1000})
		{
			const auto split = nativeDamage::hellfireBattery::splitBudget(total, share);
			EXPECT_EQ(split.physical + split.fire, total);
			EXPECT_EQ(split.fire, total * share / 1000);
		}
	const auto fractional = nativeDamage::hellfireBattery::splitBudget(51.99, 200);
	EXPECT_EQ(fractional.physical, 41);
	EXPECT_EQ(fractional.fire, 10);
}

TEST(HellfireBatteryDamage, ExtremeBudgetDoesNotOverflowAndInvalidSharesAreRejected)
{
	const auto split = nativeDamage::hellfireBattery::splitBudget(
		std::numeric_limits<double>::infinity(), 500);
	EXPECT_EQ(split.physical + split.fire, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(split.fire, std::numeric_limits<int64_t>::max() / 2);
	EXPECT_THROW(nativeDamage::hellfireBattery::splitBudget(100, -1), std::invalid_argument);
	EXPECT_THROW(nativeDamage::hellfireBattery::splitBudget(100, 1001), std::invalid_argument);
}

TEST(HellfireBatteryDamage, SharedBudgetKeepsRangeWallsAndMasterGunnerOutput)
{
	DamageAttackInfo info;
	info.shooting = true;
	info.rangedFollowUpDamagePercent = 60;
	nativeDamage::Facts facts;
	facts.distancePenalty = true;
	facts.wallPenalty = true;
	EXPECT_DOUBLE_EQ(commonFactor(info, facts), 0.15);
}

TEST(HellfireBatteryDamage, SharedBudgetExcludesPhysicalDefenseAndPhantomProtection)
{
	DamageAttackInfo info;
	info.physicalDamageReductionCapPercent = 75;
	info.intrinsicPhysicalDamageReductionPercent = 75;
	info.bulwarkImmovableFinalDamageMultiplier = 75;
	info.armorerBastionFinalDamageMultiplier = 75;
	info.frozenShatterFinalDamageMultiplier = 125;
	nativeDamage::Facts facts;
	facts.attacker.attack = 0;
	facts.defender.defense = 100;
	facts.defender.phantomIntegrity = 100;
	EXPECT_DOUBLE_EQ(commonFactor(info, facts), 1.0);
	facts.attacker.attack = 200;
	facts.defender.defense = 0;
	EXPECT_DOUBLE_EQ(commonFactor(info, facts), 1.0);
}

TEST(HellfireBatteryDamage, SharedOutgoingBoostAndLuckAreAppliedOnce)
{
	DamageAttackInfo info;
	info.executionerDamagePercent = 20;
	info.luckyStrike = true;
	info.newHorizonsFinalLuck = true;
	info.activationOutputPercent = 60;
	nativeDamage::Facts facts;
	facts.attacker.attack = 100;
	EXPECT_DOUBLE_EQ(commonFactor(info, facts), 1.44);
}

TEST(HellfireBatteryDamage, InactiveNativePathRetainsAdditiveAttackAndEveryHistoricalMultiplier)
{
	DamageAttackInfo info;
	info.shooting = true;
	info.attackFactorPerPoint = 0.05;
	info.attackFactorCap = 3;
	info.heroOrderFinalDamageMultiplier = 125;
	info.rangedFollowUpDamagePercent = 60;
	nativeDamage::Facts facts;
	facts.attacker.count = 1;
	facts.attacker.minDamage = facts.attacker.maxDamage = 101;
	facts.attacker.attack = 10;
	facts.defender.defense = 5;
	facts.defender.count = 100;
	facts.defender.firstHealth = facts.defender.maxHealth = 10;
	facts.damageBoost = 20;
	facts.distancePenalty = true;
	const auto result = nativeDamage::calculate(info, facts);
	EXPECT_EQ(result.damage.min, 54);
	EXPECT_EQ(result.damage.max, 54);
	EXPECT_EQ(result.damageBeforeDefense.min, 109);
	EXPECT_EQ(result.kills.min, 5);
}
