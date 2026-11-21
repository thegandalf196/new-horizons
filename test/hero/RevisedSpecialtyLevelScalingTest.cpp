/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsRevisedSpecialties.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/filesystem/ResourcePath.h"

namespace
{
using newHorizonsHeroes::levelScaledOrdinaryValue;
const newHorizonsHeroes::SpecialtyLevelScaling threePercent{3};
}

TEST(RevisedSpecialtyLevelScaling, AuthoredValueIsTheLevelOneValueAndGrowsThreePercentOfItself)
{
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "relentlessBarrageDamagePercent", 50, 1), 50);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "relentlessBarrageDamagePercent", 50, 0), 50);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "relentlessBarrageDamagePercent", 50, 20), 78);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "pointBlankShotDamageBonusPercent", 100, 30), 187);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "stoneSentinelReductionPercent", 20, 10), 25);
}

TEST(RevisedSpecialtyLevelScaling, HardCapsLimitReductionsAndChances)
{
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "shieldAndSwordPhysicalReductionPercent", 40, 30), 74);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "shieldAndSwordPhysicalReductionPercent", 40, 40), 75);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "dispellingShotArchMageChancePercent", 40, 99), 100);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "graveDustHealingReductionBasisPoints", 5000, 99), 10000);
}

TEST(RevisedSpecialtyLevelScaling, UndyingGrowsByFiveHealthPerLevel)
{
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "undyingHealingHP", 100, 1), 100);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "undyingHealingHP", 100, 20), 195);
}

TEST(RevisedSpecialtyLevelScaling, BonusBasedSpecialtiesUseTheirOwnSteps)
{
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "packPursuitSpeed", 5, 9), 5);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "packPursuitSpeed", 5, 10), 6);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "packPursuitSpeed", 5, 25), 7);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "resistanceAuraPoints", 10, 20), 15);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "resistanceAuraPoints", 30, 99), 75);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "runicReflectionChancePercent", 20, 20), 31);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "runicReflectionChancePercent", 40, 1), 40);
}

TEST(RevisedSpecialtyLevelScaling, UnlistedPolicyAndZeroCoefficientsAreUnchanged)
{
	EXPECT_FALSE(newHorizonsHeroes::scalesWithLevel("runicReflectionPolicyVersion"));
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "runicReflectionPolicyVersion", 1, 30), 1);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "boneFormationFirstBandSize", 10, 30), 10);
	EXPECT_EQ(levelScaledOrdinaryValue(threePercent, "relentlessBarrageDamagePercent", 0, 30), 0);
}

TEST(RevisedSpecialtyLevelScaling, ShippedRulesCaptureTheRuleAndHistoricalRulesDoNot)
{
	JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto captured = newHorizonsHeroes::specialtyLevelScaling(heroes);
	ASSERT_TRUE(captured.has_value());
	EXPECT_EQ(captured->percentOfBasePerLevel, 3);
	EXPECT_NE(newHorizonsHeroes::levelScalingDescription(*captured, "relentlessBarrageDamagePercent").find("3% of itself"),
		std::string::npos);
	EXPECT_NE(newHorizonsHeroes::levelScalingDescription(*captured, "shieldAndSwordPhysicalReductionPercent").find("up to 75%"),
		std::string::npos);
	EXPECT_TRUE(newHorizonsHeroes::levelScalingDescription(*captured, "boneFormationFirstBandSize").empty());
	heroes.Struct().erase("revisedSpecialties");
	EXPECT_FALSE(newHorizonsHeroes::specialtyLevelScaling(heroes).has_value());
}
