/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../AI/BattleAI/BattleStrengthRatio.h"
#include "../../AI/BattleAI/BattleSearchPolicy.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../mock/mock_CStack.h"
#include <vcmi/CreatureService.h>
#include <bit>
#include <cmath>
#include <limits>

namespace
{
// Only the admitted count and roster metadata vary. Creature identity/value
// resolves through the actual library exactly as in makeEvaluator's helper.
class StrengthStack final : public test::MockCStack
{
public:
	CreatureID creature;
	int32_t count = 0;
	bool ghost = false;
	bool turret = false;
	bool living = true;
	CreatureID creatureId() const override { return creature; }
	int32_t getCount() const override { return count; }
	int32_t creatureIndex() const override { return turret ? CreatureID::ARROW_TOWERS : creature.getNum(); }
	bool isGhost() const override { return ghost; }
	bool alive() const override { return living; }
};

class BattleStrengthRatioTest : public ::testing::Test
{
protected:
	static void SetUpTestSuite() { VCMI_TEST_REQUIRE_ORIGINAL_DATA(); }
	BattleInfo battle{nullptr};
	CPlayerBattleCallback callback{&battle, PlayerColor(0)};
	CreatureID strongest;
	CreatureID pikeman;
	void SetUp() override
	{
		ASSERT_NE(LIBRARY, nullptr);
		pikeman = CreatureID(CreatureID::decode("core:pikeman"));
		ASSERT_TRUE(pikeman.hasValue());
		int32_t greatestValue = 0;
		LIBRARY->creatures()->forEach([&](const Creature * creature, bool &)
		{
			if(creature->getModScope() == "core" && creature->getAIValue() > greatestValue)
			{
				strongest = creature->getId();
				greatestValue = creature->getAIValue();
			}
		});
		ASSERT_TRUE(strongest.hasValue());
		ASSERT_GT(greatestValue, 1);
	}
	StrengthStack & add(BattleSide side, int32_t count, CreatureID creature)
	{
		auto stack = std::make_unique<StrengthStack>();
		stack->mockedSide = side;
		stack->creature = creature;
		stack->count = count;
		auto & result = *stack;
		battle.stacks.push_back(std::move(stack));
		return result;
	}
};
}

TEST_F(BattleStrengthRatioTest, IndividualProductBeyondSignedIntUsesRealCreatureValue)
{
	const auto count = std::numeric_limits<int32_t>::max();
	add(BattleSide::ATTACKER, count, strongest);
	auto & enemy = add(BattleSide::DEFENDER, count, strongest);
	EXPECT_FLOAT_EQ(battleAI::strengthRatio(callback, BattleSide::ATTACKER), 1.0f);
	// An overflowing negative product must not cancel a positive denominator.
	enemy.count = 1;
	EXPECT_FLOAT_EQ(battleAI::strengthRatio(callback, BattleSide::ATTACKER), static_cast<float>(count));
}

TEST_F(BattleStrengthRatioTest, MultipleIndividuallySafeProductsCanOverflowSignedSum)
{
	const auto value = strongest.toCreature()->getAIValue();
	const auto count = std::numeric_limits<int32_t>::max() / value;
	ASSERT_GT(count, 0);
	for(int index = 0; index < 3; ++index)
		add(BattleSide::ATTACKER, count, strongest);
	add(BattleSide::DEFENDER, count, strongest);
	EXPECT_FLOAT_EQ(battleAI::strengthRatio(callback, BattleSide::ATTACKER), 3.0f);
}

TEST_F(BattleStrengthRatioTest, SevenMaximumCountStacksStayFiniteUnderFullAndFastPolicies)
{
	const auto count = std::numeric_limits<int32_t>::max();
	for(int index = 0; index < 7; ++index)
		add(BattleSide::ATTACKER, count, strongest);
	add(BattleSide::DEFENDER, count, strongest);
	for(const auto profile : {battleSearch::Profile::Full, battleSearch::Profile::BeamOneTwoActors})
	{
		const battleSearch::Scope policy(profile);
		const auto ratio = battleAI::strengthRatio(callback, BattleSide::ATTACKER);
		EXPECT_TRUE(std::isfinite(ratio));
		EXPECT_FLOAT_EQ(ratio, 7.0f);
	}
}

TEST_F(BattleStrengthRatioTest, OrdinaryRatiosRetainSeparateFloatRoundingBits)
{
	const auto value = pikeman.toCreature()->getAIValue();
	ASSERT_GT(value, 0);
	constexpr int32_t ourCount = 16777200;
	constexpr int32_t enemyCount = 16777201;
	ASSERT_LE(value, std::numeric_limits<int32_t>::max() / enemyCount);
	// All historical intermediate integer operations are defined in this case.
	// This reference freezes the prior float boundary, not the new wide sums.
	const int32_t our = ourCount * value;
	const int32_t enemy = enemyCount * value;
	const float previous = static_cast<float>(our) / static_cast<float>(enemy);
	const float wide = static_cast<float>(static_cast<long double>(our) / enemy);
	ASSERT_NE(std::bit_cast<uint32_t>(previous), std::bit_cast<uint32_t>(wide));
	add(BattleSide::ATTACKER, ourCount, pikeman);
	add(BattleSide::DEFENDER, enemyCount, pikeman);
	EXPECT_EQ(std::bit_cast<uint32_t>(battleAI::strengthRatio(callback, BattleSide::ATTACKER)),
		std::bit_cast<uint32_t>(previous));
	EXPECT_FLOAT_EQ(battleAI::strengthRatio(callback, BattleSide::DEFENDER),
		static_cast<float>(enemy) / static_cast<float>(our));
}

TEST_F(BattleStrengthRatioTest, EmptyOrZeroEnemyKeepsExistingFallback)
{
	EXPECT_FLOAT_EQ(battleAI::strengthRatio(callback, BattleSide::ATTACKER), 1.0f);
	add(BattleSide::ATTACKER, 123, pikeman);
	EXPECT_FLOAT_EQ(battleAI::strengthRatio(callback, BattleSide::ATTACKER), 1.0f);
	add(BattleSide::DEFENDER, 0, pikeman);
	EXPECT_FLOAT_EQ(battleAI::strengthRatio(callback, BattleSide::ATTACKER), 1.0f);
}

TEST_F(BattleStrengthRatioTest, ExistingGhostTurretAndDeadRosterEligibilityIsUnchanged)
{
	add(BattleSide::ATTACKER, 20, pikeman).living = false;
	add(BattleSide::DEFENDER, 10, pikeman);
	add(BattleSide::ATTACKER, std::numeric_limits<int32_t>::max(), strongest).ghost = true;
	add(BattleSide::DEFENDER, std::numeric_limits<int32_t>::max(), strongest).turret = true;
	EXPECT_FLOAT_EQ(battleAI::strengthRatio(callback, BattleSide::ATTACKER), 2.0f);
}
