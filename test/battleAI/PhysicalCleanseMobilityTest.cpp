/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../server/battles/NewHorizonsMachineSpecialtyFixture.h"
#include "../../AI/BattleAI/AttackControllerProjection.h"
#include "../../AI/BattleAI/PhysicalCleanseValue.h"
class PhysicalCleanseMobilityTest : public NewHorizonsMachineSpecialtyFixture
{
protected:
	std::shared_ptr<heroQueueTest::QueueEnvironment> environment;
	std::shared_ptr<HypotheticBattle> branch;
	void projection()
	{
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(const auto * state = dynamic_cast<const battle::CUnitState *>(unit))
			{ (void)state->counterAttacks.available(); (void)state->shots.available(); (void)state->casts.available(); }
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		const std::shared_ptr<CBattleInfoCallback> known(gameState(), battle());
		branch = std::make_shared<HypotheticBattle>(environment.get(), known);
		const auto * source = branch->battleGetUnitByID(actor->unitId());
		const auto * target = branch->battleGetUnitByID(victim->unitId());
		attackControllerAI::applyCripple(*branch, attackControllerAI::capture(*branch,
			BattleAttackInfo(source, target, 0, true), target->getPosition(), 1));
	}
};
TEST_F(PhysicalCleanseMobilityTest, ThreeSpeedRestorationCreditsOnlyNewlyReachableLegalPressureAndDoesNotMutate)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gerwulf", 0, false));
	ASSERT_NO_FATAL_FAILURE(projection());
	const auto * unit = branch->battleGetUnitByID(victim->unitId());
	const auto state = heroQueueTest::wire(unit->acquireState()->save());
	const auto native = heroQueueTest::wire(*gameState());
	const auto version = branch->getTreeVersion();
	physicalCleanseAI::MobilityQuery query;
	const auto value = query.crippleValue(*branch, *unit);
	EXPECT_GT(value, 0);
	EXPECT_EQ(query.crippleValue(*branch, *unit), value);
	EXPECT_EQ(heroQueueTest::wire(unit->acquireState()->save()), state);
	EXPECT_EQ(heroQueueTest::wire(*gameState()), native);
	EXPECT_EQ(branch->getTreeVersion(), version);
}
TEST_F(PhysicalCleanseMobilityTest, AdjacentOrProhibitedTargetsDoNotBecomeAnotherRecoveredActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gerwulf", 0, false));
	victim->setPosition(BattleHex(4, 5));
	ASSERT_NO_FATAL_FAILURE(projection());
	physicalCleanseAI::MobilityQuery adjacent;
	EXPECT_EQ(adjacent.crippleValue(*branch, *branch->battleGetUnitByID(victim->unitId())), 0);
	for(const auto * unit : branch->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::ATTACKER)
			branch->addUnitBonus(unit->unitId(), {Bonus(BonusDuration::ONE_BATTLE,
				BonusType::CREATURE_ATTACK_TARGET_PROTECTION, BonusSource::OTHER, 1, BonusSourceID())});
	physicalCleanseAI::MobilityQuery prohibited;
	EXPECT_EQ(prohibited.crippleValue(*branch, *branch->battleGetUnitByID(victim->unitId())), 0);
}
TEST_F(PhysicalCleanseMobilityTest, OccupiedRingBlocksPressureEvenWhenMovementRadiusIncreases)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gerwulf", 0, false));
	for(const auto & cell : victim->getPosition().getNeighbouringTiles())
		if(!battle()->battleGetUnitByPos(cell))
			ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), cell, 1), nullptr);
	ASSERT_NO_FATAL_FAILURE(projection());
	physicalCleanseAI::MobilityQuery query;
	EXPECT_EQ(query.crippleValue(*branch, *branch->battleGetUnitByID(victim->unitId())), 0);
}
TEST_F(PhysicalCleanseMobilityTest, ExpiredNoncanonicalAndImmobilizedStatusesHaveNoInventedMobilityValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gerwulf", 0, false));
	ASSERT_NO_FATAL_FAILURE(projection());
	const auto target = branch->getForUpdate(victim->unitId());
	branch->removeUnitBonus(target->unitId(), newHorizonsAttackControllers::crippleRemoval(*target));
	branch->addUnitBonus(target->unitId(), {Bonus(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER, -3, BonusSourceID())});
	physicalCleanseAI::MobilityQuery unrelated;
	EXPECT_EQ(unrelated.crippleValue(*branch, *target), 0);
	auto expired = attackControllerAI::capture(*branch,
		BattleAttackInfo(branch->battleGetUnitByID(actor->unitId()), target.get(), 0, true), target->getPosition(), 1);
	for(auto & bonus : expired.cripple.add) bonus.turnsRemain = 0;
	attackControllerAI::applyCripple(*branch, expired);
	physicalCleanseAI::MobilityQuery zero;
	EXPECT_EQ(zero.crippleValue(*branch, *target), 0);
	for(auto & bonus : expired.cripple.add) bonus.turnsRemain = 3;
	expired.cripple.remove = newHorizonsAttackControllers::crippleRemoval(*target);
	attackControllerAI::applyCripple(*branch, expired);
	branch->addUnitBonus(target->unitId(), {Bonus(BonusDuration::ONE_BATTLE,
		BonusType::BIND_EFFECT, BonusSource::OTHER, 1, BonusSourceID())});
	physicalCleanseAI::MobilityQuery immobile;
	EXPECT_EQ(immobile.crippleValue(*branch, *target), 0);
}
