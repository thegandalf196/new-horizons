/*
 * NewHorizonsTimeStopAITest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../mock/BattleFake.h"
#include "../mock/mock_spells_Mechanics.h"
#include "../mock/mock_CStack.h"
#include "AI/BattleAI/SpellTargetsEvaluator.h"
#include "lib/GameLibrary.h"
#include "lib/modding/CModHandler.h"
#include "lib/spells/CSpell.h"
#include "lib/spells/NewHorizonsSorcery.h"

namespace
{
using namespace testing;

class TimeStopStackMock : public test::MockCStack
{
public:
	MOCK_CONST_METHOD0(alive, bool());
	MOCK_CONST_METHOD0(isTimeStopped, bool());
	MOCK_CONST_METHOD0(getAvailableHealth, int64_t());
	MOCK_CONST_METHOD0(getTotalHealth, int64_t());
	MOCK_CONST_METHOD1(willMove, bool(int));
};

SpellID timeStopSpell()
{
	return SpellID(SpellID::decode(newHorizonsSorcery::TIME_STOP_SPELL));
}
}

using namespace testing;

class NewHorizonsTimeStopAITest : public ::testing::Test
{
protected:
	test::battle::BattleFake battle;
	spells::MechanicsMock mechanics;
	TimeStopStackMock enemy;
	TimeStopStackMock healthyAlly;
	TimeStopStackMock injuredAlly;
	SpellID spell;

	void SetUp() override
	{
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		spell = timeStopSpell();
		ASSERT_TRUE(spell.hasValue());
		ASSERT_NE(spell.toSpell(), nullptr);

		ON_CALL(battle, getSidePlayer(BattleSide::ATTACKER)).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(battle, getSidePlayer(BattleSide::DEFENDER)).WillByDefault(Return(PlayerColor(1)));
		ON_CALL(mechanics, battle()).WillByDefault(Return(&battle));
		ON_CALL(mechanics, getSpell()).WillByDefault(Return(spell.toSpell()));
		ON_CALL(mechanics, getCasterColor()).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(mechanics, canBeCastAt(_, _)).WillByDefault(Return(true));

		enemy.mockedSide = BattleSide::DEFENDER;
		healthyAlly.mockedSide = BattleSide::ATTACKER;
		injuredAlly.mockedSide = BattleSide::ATTACKER;
		for(auto * stack : {&enemy, &healthyAlly, &injuredAlly})
		{
			ON_CALL(*stack, alive()).WillByDefault(Return(true));
			ON_CALL(*stack, isTimeStopped()).WillByDefault(Return(false));
			ON_CALL(*stack, willMove(_)).WillByDefault(Return(true));
		}
	}
};

TEST_F(NewHorizonsTimeStopAITest, LegalTargetEnumerationUsesBattlefieldHexes)
{
	ON_CALL(mechanics, getTargetTypes()).WillByDefault(Return(std::vector<spells::AimType>{spells::AimType::LOCATION}));
	const auto targets = SpellTargetEvaluator::getViableTargets(&mechanics);
	ASSERT_FALSE(targets.empty());
	for(const auto & target : targets)
	{
		ASSERT_EQ(target.size(), 1u);
		EXPECT_TRUE(target.front().hexValue.isValid());
		EXPECT_EQ(target.front().unitValue, nullptr);
	}
}

TEST_F(NewHorizonsTimeStopAITest, ValuesEnemyPressureAndThreatenedAllyButRejectsHealthyAlly)
{
	ON_CALL(enemy, getAvailableHealth()).WillByDefault(Return(100));
	ON_CALL(enemy, getTotalHealth()).WillByDefault(Return(100));
	ON_CALL(healthyAlly, getAvailableHealth()).WillByDefault(Return(100));
	ON_CALL(healthyAlly, getTotalHealth()).WillByDefault(Return(100));
	ON_CALL(injuredAlly, getAvailableHealth()).WillByDefault(Return(20));
	ON_CALL(injuredAlly, getTotalHealth()).WillByDefault(Return(100));

	const spells::Target target{spells::Destination(BattleHex(70))};
	ON_CALL(mechanics, getAffectedStacks(_)).WillByDefault(Return(std::vector<const CStack *>{&enemy}));
	const auto enemyValue = SpellTargetEvaluator::timeStopPlacementValue(&mechanics, target);
	EXPECT_GT(enemyValue, 0.0f);

	ON_CALL(mechanics, getAffectedStacks(_)).WillByDefault(Return(std::vector<const CStack *>{&healthyAlly}));
	const auto healthyValue = SpellTargetEvaluator::timeStopPlacementValue(&mechanics, target);
	EXPECT_LT(healthyValue, 0.0f);

	ON_CALL(mechanics, getAffectedStacks(_)).WillByDefault(Return(std::vector<const CStack *>{&injuredAlly}));
	const auto injuredValue = SpellTargetEvaluator::timeStopPlacementValue(&mechanics, target);
	EXPECT_GT(injuredValue, 0.0f);
}
