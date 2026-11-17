/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../AI/BattleAI/MetamagicExpectedValue.h"
namespace
{
const newHorizonsMetamagic::ResolutionPolicy policy{{0, 20, 30, 40}, 10, 15, 0};
newHorizonsMetamagic::RemovalCandidate effect(int mana, int spell)
{
	newHorizonsMetamagic::RemovalCandidate result;
	result.recipient = 10;
	result.spell = result.family = SpellID(spell);
	result.caster = PlayerColor(1);
	result.listedMana = mana;
	return result;
}
TEST(MetamagicExpectedValueTest, CurseAndSlowUseOneRestorationPoolWithoutSpellPower)
{
	const std::array effects{effect(4, 1), effect(11, 2)};
	const auto result = metamagicAI::expectedPool(effects, policy, 3, false,
		newHorizonsMetamagic::ResolutionKind::RESTORATION, 1000);
	ASSERT_TRUE(result);
	EXPECT_DOUBLE_EQ(result->hitPoints, 90);
	EXPECT_DOUBLE_EQ(result->removals, 0.8);
}
TEST(MetamagicExpectedValueTest, CapacityAppliesToEachCombinedOutcomeBeforeExpectation)
{
	const std::array effects{effect(4, 1), effect(11, 2)};
	const auto result = metamagicAI::expectedPool(effects, policy, 3, false,
		newHorizonsMetamagic::ResolutionKind::RESTORATION, 100);
	ASSERT_TRUE(result);
	// Neither .36; Curse .24; Slow .24; both .16. Combined pool caps at 100.
	EXPECT_DOUBLE_EQ(result->hitPoints, 54.4);
}
TEST(MetamagicExpectedValueTest, ChainRevisitsDoNotDuplicateEffectOrDischarge)
{
	const auto original = effect(11, 2);
	const std::array effects{original, original};
	const auto result = metamagicAI::expectedPool(effects, policy, 1, false,
		newHorizonsMetamagic::ResolutionKind::OFFENSE, 1000);
	ASSERT_TRUE(result);
	EXPECT_DOUBLE_EQ(result->hitPoints, 22);
	EXPECT_DOUBLE_EQ(result->removals, 0.2);
}
TEST(MetamagicExpectedValueTest, UnsupportedRecipientMixAndUnboundedWorkRemainUnknown)
{
	auto other = effect(4, 1); other.recipient = 11;
	const std::array mixed{effect(4, 1), other};
	EXPECT_FALSE(metamagicAI::expectedPool(mixed, policy, 3, false,
		newHorizonsMetamagic::ResolutionKind::RESTORATION, 100));
	const std::vector tooMany(65, effect(4, 1));
	EXPECT_FALSE(metamagicAI::expectedPool(tooMany, policy, 3, false,
		newHorizonsMetamagic::ResolutionKind::RESTORATION, 100));
}
TEST(MetamagicExpectedValueTest, CapturedPolicyAndHalonChanceAreUsedWithoutChangingPoolFormula)
{
	const newHorizonsMetamagic::ResolutionPolicy captured{{0, 10, 20, 30}, 8, 12, 5};
	const std::array effects{effect(4, 1)};
	const auto result = metamagicAI::expectedPool(effects, captured, 3, true,
		newHorizonsMetamagic::ResolutionKind::OFFENSE, 1000);
	ASSERT_TRUE(result);
	EXPECT_DOUBLE_EQ(result->hitPoints, 11.2);
	EXPECT_DOUBLE_EQ(result->removals, 0.35);
}

}
