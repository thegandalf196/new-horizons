/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleRoundHealing.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include <string>
namespace
{
class IncomingRegenerationReceiptTest : public BattleTestFixture,
	public ::testing::WithParamInterface<int> {};
TEST_P(IncomingRegenerationReceiptTest, AuthoritativeNativeRegenerationCommitsOnlyReceivedHealth)
{
	startGame();
	startBattle();
	auto * unit = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(leftHex), 3);
	ASSERT_NE(unit, nullptr);
	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HP_REGENERATION,
		BonusSource::CREATURE_ABILITY, 100, BonusSourceID()));
	const int reduction = GetParam();
	if(reduction >= 0)
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::STACK_ACTIVATION,
			BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS, BonusSource::OTHER,
			reduction, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
	auto wounded = unit->acquireState();
	int64_t damage = 9;
	wounded->damage(damage);
	ASSERT_EQ(damage, 9);
	BattleUnitsChanged injury;
	injury.battleID = battle()->getBattleID();
	UnitChanges change(unit->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = wounded->save();
	change.healthDelta = -damage;
	injury.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(injury);
	const auto before = unit->getAvailableHealth();
	server.battleLogLines.clear();
	server.battleAnimations.clear();
	battleRoundHealing::beforeTurn(*gameHandler, *battle(), unit);
	const int64_t expected = reduction < 0 ? 9 : 9 - 9 * reduction / 10'000;
	EXPECT_EQ(unit->getAvailableHealth(), before + expected);
	EXPECT_EQ(unit->getCount(), 3);
	if(reduction > 0 && expected > 0)
	{
		ASSERT_EQ(server.battleLogLines.size(), 1u);
		ASSERT_EQ(server.battleAnimations.size(), 1u);
		const auto & animation = server.battleAnimations.front();
		EXPECT_EQ(animation.animation, AnimationPath::builtin("SP12_.DEF"));
		EXPECT_EQ(animation.sound, AudioPath::builtin("REGENER"));
		EXPECT_FLOAT_EQ(animation.transparency, 0.5f);
		EXPECT_FALSE(animation.deferred);
		ASSERT_EQ(animation.targets.size(), 1u);
		EXPECT_EQ(animation.targets.front().unitID, static_cast<int32_t>(unit->unitId()));
		EXPECT_EQ(animation.targets.front().tile, unit->getPosition());
		EXPECT_NE(server.battleLogLines.front().find(std::to_string(expected) + " Health."), std::string::npos);
	}
	if(reduction == 10'000) EXPECT_TRUE(server.battleLogLines.empty());
	if(reduction <= 0 || expected == 0) EXPECT_TRUE(server.battleAnimations.empty());
}
INSTANTIATE_TEST_SUITE_P(AbsentZeroPartialFull, IncomingRegenerationReceiptTest,
	::testing::Values(-1, 0, 5000, 10'000));
}
