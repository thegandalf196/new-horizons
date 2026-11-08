/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/BarrageValue.h"
using namespace heroQueueTest;
class BarrageValueTest : public HeroQueueSelectorTest
{
protected:
	BarrageValueTest() { useRevisedOrders = true; }
	CStack * marksman = nullptr;
	void prepare()
	{
		world(); attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:valeska")));
		arena();
		marksman = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(6, 3), 10);
		ASSERT_NE(marksman, nullptr);
		effect(marksman, Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()));
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(const auto * state = dynamic_cast<const battle::CUnitState *>(unit))
			{ (void)state->counterAttacks.available(); (void)state->shots.available(); (void)state->casts.available(); }
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
};
TEST_F(BarrageValueTest, FractionalOpportunityIsLinearAndNeverSpendsForecastAmmoOrHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto source = child->getForUpdate(marksman->unitId()), target = child->getForUpdate(enemy->unitId());
	AttackPossibility nominal(source->getPosition(), target->getPosition(), BattleAttackInfo(source.get(), target.get(), 0, true));
	nominal.attackerState = source;
	const auto prepared = barrageAI::prepare(nominal, 2, child);
	ASSERT_TRUE(prepared); EXPECT_GT(prepared->probability, 0);
	nominal.affectedUnits.push_back(target);
	const auto sourceBefore = wire(source->save()), targetBefore = wire(target->save());
	const auto outside = observation();
	DamageCache cache;
	auto certain = *prepared; certain.probability = 1;
	const auto full = barrageAI::value(nominal, certain, cache, child);
	ASSERT_GT(full, 0);
	auto half = certain; half.probability = 0.5L;
	EXPECT_FLOAT_EQ(barrageAI::value(nominal, half, cache, child), full / 2);
	EXPECT_EQ(wire(source->save()), sourceBefore); EXPECT_EQ(wire(target->save()), targetBefore);
	EXPECT_EQ(observation(), outside);
}
TEST_F(BarrageValueTest, RemainingAmmoSameTargetSurvivalAndNonValeskaAdmissionBoundRepeatValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto source = child->getForUpdate(marksman->unitId()), target = child->getForUpdate(enemy->unitId());
	AttackPossibility nominal(source->getPosition(), target->getPosition(), BattleAttackInfo(source.get(), target.get(), 0, true));
	nominal.attackerState = source; nominal.affectedUnits.push_back(target);
	DamageCache cache;
	const barrageAI::Prepared certain{1, 2, 50};
	const auto two = barrageAI::value(nominal, certain, cache, child);
	ASSERT_GT(two, 0);
	source->shots.use(source->shots.available() - 1);
	EXPECT_LE(barrageAI::value(nominal, certain, cache, child), two);
	source->shots.use(1);
	EXPECT_EQ(barrageAI::value(nominal, certain, cache, child), 0);
	EXPECT_FALSE(barrageAI::prepare(nominal, 2, child));
	int64_t kill = target->getAvailableHealth(); target->damage(kill);
	EXPECT_EQ(barrageAI::value(nominal, certain, cache, child), 0);
	AttackPossibility unrelated(actor->getPosition(), enemy->getPosition(), BattleAttackInfo(actor, enemy, 0, false));
	unrelated.attackerState = child->getForUpdate(actor->unitId());
	EXPECT_FALSE(barrageAI::prepare(unrelated, 1, child));
}
TEST_F(BarrageValueTest, OpposingObserverDoesNotInferDormantBarragePolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto hidden = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto opponent = std::make_shared<HypotheticBattle>(environment.get(), hidden);
	const auto source = opponent->getForUpdate(marksman->unitId()), target = opponent->getForUpdate(enemy->unitId());
	AttackPossibility nominal(source->getPosition(), target->getPosition(), BattleAttackInfo(source.get(), target.get(), 0, true));
	nominal.attackerState = source;
	EXPECT_FALSE(barrageAI::prepare(nominal, 2, opponent));
}
