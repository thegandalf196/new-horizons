/*
 * OwnedExportLifecycleTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OwnedExportLifecycleFixture.h"

TEST_F(OwnedExportLifecycleTest, NormalActivationUsesActualPacketExpiryAndScalarHooks)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	checkTurn(BattleUnitTurnReason::TURN_QUEUE);
}

TEST_F(OwnedExportLifecycleTest, MoraleActivationUsesActualPacketExpiryAndScalarHooks)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	checkTurn(BattleUnitTurnReason::MORALE);
}

TEST_F(OwnedExportLifecycleTest, OrdinaryAutomaticActivationUsesActualPacketExpiryAndScalarHooks)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	checkTurn(BattleUnitTurnReason::AUTOMATIC_ACTION);
}

TEST_F(OwnedExportLifecycleTest, ReducedExtraActivationUsesActualPacketExpiryAndScalarHooks)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	checkTurn(BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION);
}

TEST_F(OwnedExportLifecycleTest, ArmedSecondWindExpiresActivationNotUnrelatedGetsTurnRecords)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	incoming->movedThisRound = true;
	const auto order = battle()->battlePrepareHeroOrderState(BattleSide::DEFENDER,
		HeroCommand::SECOND_WIND, {incoming->unitId()});
	ASSERT_TRUE(order);
	battle()->setHeroOrderState(BattleSide::DEFENDER, order);
	ASSERT_TRUE(battle()->setHeroOrderSecondWindActive(BattleSide::DEFENDER, true));
	checkTurn(BattleUnitTurnReason::HERO_COMMAND);
}

TEST_F(OwnedExportLifecycleTest, SpellAndPhysicalContinuationsPreserveGenuineActivationExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	incoming->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION,
		BonusType::STACKS_SPEED, BonusSource::OTHER, -2, BonusSourceID()));
	captureOwned();
	startOwned();
	const auto before = known->battleGetUnitByID(incoming->unitId())->getMovementRange(0);
	for(const auto reason : {BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST,
		BattleUnitTurnReason::MASTER_GATE_CONTINUATION, BattleUnitTurnReason::PURSUIT_CONTINUATION,
		BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION})
	{
		known->nextTurn(incoming->unitId(), reason);
		EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getMovementRange(0), before);
	}
	const auto expected = freezeFrame();
	retireFrame();
	for(const auto reason : {BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST,
		BattleUnitTurnReason::MASTER_GATE_CONTINUATION, BattleUnitTurnReason::PURSUIT_CONTINUATION,
		BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION})
		turnPacket(reason);
	compareFrame(expected);
}

TEST_F(OwnedExportLifecycleTest, CanonicalRoundTimerHistoryForksWithoutMutatingNativeOrSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	auto value = aura(BonusDuration::N_TURNS);
	value.turnsRemain = 2;
	incoming->addNewBonus(std::make_shared<Bonus>(value));
	ASSERT_GT(braced->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)), 0);
	captureOwned();
	startOwned();
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
	const auto original = incoming->acquireState()->save();
	const auto authorityRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	known->nextRound();
	known = std::make_shared<HypotheticBattle>(environment.get(), known);
	known->nextRound();
	const auto expected = freezeFrame();
	EXPECT_EQ(incoming->acquireState()->save(), original);
	EXPECT_EQ(sibling->getRound(), expected.round - 2);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), authorityRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
	sibling.reset();
	retireFrame();
	roundPacket();
	roundPacket();
	compareFrame(expected);
}

TEST_F(OwnedExportLifecycleTest, OriginatingAddClockRemovalAndForkRetainExactHandleProvenance)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	captureOwned();
	auto value = aura(BonusDuration::N_TURNS);
	value.turnsRemain = 3;
	const auto handle = addOrigin(*incoming, value);
	startOwned();
	known->nextRound();
	auto next = std::make_shared<HypotheticBattle>(environment.get(), known);
	const auto base = known->battleGetUnitByID(braced->unitId())->getAttack(false);
	next->initialExportSubject()->world().remove(handle);
	EXPECT_EQ(next->battleGetUnitByID(braced->unitId())->getAttack(false), base - 4);
	EXPECT_EQ(known->battleGetUnitByID(braced->unitId())->getAttack(false), base);
	const auto expected = next->battleGetUnitByID(braced->unitId())->getAttack(false);
	next.reset();
	retireFrame();
	const auto nativeRecord = std::make_shared<Bonus>(value);
	incoming->addNewBonus(nativeRecord);
	roundPacket();
	incoming->removeBonus(nativeRecord);
	EXPECT_EQ(braced->getAttack(false), expected);
}

TEST_F(OwnedExportLifecycleTest, FortuneEnvironmentTracksActivationAndRoundBeforeForkQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	auto fortune = battle()->getSylvanLuckState(BattleSide::DEFENDER);
	fortune.forestsFavor = true;
	fortune.speedUnits.insert(incoming->unitId());
	battle()->getSide(BattleSide::DEFENDER).sylvanLuck = fortune;
	captureOwned();
	startOwned();
	const auto before = known->battleGetUnitByID(incoming->unitId())->getMovementRange(0);
	known->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getMovementRange(0), before - 2);
	known = std::make_shared<HypotheticBattle>(environment.get(), known);
	known->nextRound();
	const auto expected = freezeFrame();
	retireFrame();
	turnPacket(BattleUnitTurnReason::TURN_QUEUE);
	roundPacket();
	compareFrame(expected);
}
