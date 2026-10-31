/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/battle/GraveDustApplication.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"
#include "../../../lib/battle/HealingReceipt.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include <stdexcept>
namespace
{
class GraveDustApplicationTest : public BattleTestFixture
{
protected:
	CStack * target = nullptr;
	void prepare()
	{
		startGame();
		startBattle();
		target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 2);
		ASSERT_NE(target, nullptr);
		beginCombat();
	}
	void apply(const newHorizonsOrdinaryStrike::EffectPlan & plan)
	{
		SetStackEffect effects;
		effects.battleID = battle()->getBattleID();
		if(!plan.remove.empty()) effects.toRemove.emplace_back(plan.target, plan.remove);
		if(!plan.add.empty()) effects.toAdd.emplace_back(plan.target, plan.add);
		gameHandler->sendAndApply(effects);
	}
	PlayerColor caster()
	{
		const auto actual = target->getUnstackedBonuses(CSelector([](const Bonus * bonus)
		{
			return bonus && newHorizonsGraveDust::isReservedSource(*bonus);
		}));
		return actual->empty() ? PlayerColor::CANNOT_DETERMINE : actual->front()->spellCasterOwner;
	}
};
TEST_F(GraveDustApplicationTest, WeakAndEqualAreExactNoOpsStrongerRetainsOrderAndChangesCaster)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto initial = newHorizonsGraveDust::prepareApplication(*target, 5000, PlayerColor(0));
	ASSERT_EQ(initial.add.size(), 2u);
	ASSERT_TRUE(initial.remove.empty());
	ASSERT_NO_THROW(apply(initial));
	const auto oldOrder = physicalAfflictions::enumerate(*target).front().applicationOrder;
	ASSERT_GT(oldOrder, 0u);
	for(const auto value : {2500, 5000})
	{
		const auto noChange = newHorizonsGraveDust::prepareApplication(*target, value, PlayerColor(1));
		EXPECT_TRUE(noChange.add.empty());
		EXPECT_TRUE(noChange.remove.empty());
		EXPECT_EQ(caster(), PlayerColor(0));
		EXPECT_EQ(physicalAfflictions::enumerate(*target).front().applicationOrder, oldOrder);
		EXPECT_EQ(battle::healingReceivedReduction(*target), 5000);
	}
	const auto stronger = newHorizonsGraveDust::prepareApplication(*target, 7500, PlayerColor(1));
	ASSERT_EQ(stronger.remove.size(), 2u);
	ASSERT_EQ(stronger.add.size(), 2u);
	EXPECT_EQ(physicalAfflictions::markerMetadata(stronger.add.front())->applicationOrder, oldOrder);
	ASSERT_NO_THROW(apply(stronger));
	EXPECT_EQ(battle::healingReceivedReduction(*target), 7500);
	EXPECT_EQ(caster(), PlayerColor(1));
	EXPECT_EQ(physicalAfflictions::enumerate(*target).front().applicationOrder, oldOrder);
}
TEST_F(GraveDustApplicationTest, ExplicitZeroChangesNothingAndInvalidBoundsFail)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto zero = newHorizonsGraveDust::prepareApplication(*target, 0, PlayerColor::CANNOT_DETERMINE);
	EXPECT_TRUE(zero.add.empty());
	EXPECT_TRUE(zero.remove.empty());
	EXPECT_EQ(battle::healingReceivedReduction(*target), 0);
	EXPECT_THROW(newHorizonsGraveDust::prepareApplication(*target, -1, PlayerColor(0)), std::invalid_argument);
	EXPECT_THROW(newHorizonsGraveDust::prepareApplication(*target, 10'001, PlayerColor(0)), std::invalid_argument);
}
TEST_F(GraveDustApplicationTest, MalformedExistingGroupCannotBeHiddenByAWeakerApplication)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const auto localCount = target->getExportedBonusList().size();
	target->addNewBonus(std::make_shared<Bonus>(dust.back())); // Deliberately malformed local setup.
	EXPECT_THROW(newHorizonsGraveDust::prepareApplication(*target, 1, PlayerColor(0)), std::invalid_argument);
	EXPECT_EQ(target->getExportedBonusList().size(), localCount + 1);
}
TEST_F(GraveDustApplicationTest, DeadOrTimeStoppedRecipientsGetNoApplicationPlan)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	target->addNewBonus(std::make_shared<Bonus>(stopped));
	ASSERT_TRUE(target->isTimeStopped());
	EXPECT_TRUE(newHorizonsGraveDust::prepareApplication(*target, 5000, PlayerColor(0)).add.empty());
	battle()->removeUnitBonus(target->unitId(), std::vector<Bonus>{stopped});
	ASSERT_FALSE(target->isTimeStopped());
	int64_t lethal = target->getAvailableHealth();
	target->damage(lethal);
	ASSERT_FALSE(target->alive());
	EXPECT_TRUE(newHorizonsGraveDust::prepareApplication(*target, 5000, PlayerColor(0)).add.empty());
}
}
