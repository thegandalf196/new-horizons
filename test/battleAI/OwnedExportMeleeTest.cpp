/*
 * OwnedExportMeleeTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OwnedExportMeleeFixture.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/UnitAdditionRules.h"
#include "../../lib/int3.h"
#include <type_traits>

static_assert(!std::is_abstract_v<InitialExportBattleSubject>);

TEST_F(OwnedExportMeleeTest, SourceInterfaceRejectsMutationWithoutConsumingBranchIdsOrRandomness)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	captureOwned();
	const auto original = incoming->acquireState()->save();
	const auto nativeRound = battle()->getRound();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto & damageRng = dynamic_cast<CRandomGenerator &>(gameHandler->randomizer->getDefault());
	const auto damageRngBefore = projectedHitTest::serialized(damageRng);
	EXPECT_EQ(seed->getBattleID(), battle()->getBattleID());
	EXPECT_EQ(&seed->getScriptContextPool(), &battle()->getScriptContextPool());
	EXPECT_EQ(seed->getLocation(), battle()->getLocation());
	EXPECT_EQ(seed->getUsedSpells(BattleSide::ATTACKER), battle()->getUsedSpells(BattleSide::ATTACKER));
	const auto layout = seed->getLayout(), nativeLayout = battle()->getLayout();
	EXPECT_EQ(layout.units, nativeLayout.units);
	EXPECT_EQ(layout.warMachines, nativeLayout.warMachines);
	EXPECT_EQ(layout.commanders, nativeLayout.commanders);
	EXPECT_EQ(layout.tacticsAllowed, nativeLayout.tacticsAllowed);
	EXPECT_EQ(layout.obstaclesAllowed, nativeLayout.obstaclesAllowed);
	const auto sourceId = battle::nextUnitAdditionId(seed->world().size(), seed->getHeroCommandRules());
	EXPECT_EQ(seed->nextUnitId(), sourceId);
	EXPECT_EQ(seed->nextUnitId(), sourceId);
	const DamageRange range{10, 20};
	EXPECT_THROW(seed->getActualDamage(range, 10, damageRng), std::logic_error);
	EXPECT_THROW(seed->nextRound(), std::logic_error);
	EXPECT_THROW(seed->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE), std::logic_error);
	EXPECT_THROW(seed->updateUnit(incoming->unitId(), original, 0), std::logic_error);
	EXPECT_THROW(seed->moveUnit(incoming->unitId(), BattleHex(92)), std::logic_error);
	EXPECT_THROW(seed->removeUnit(incoming->unitId()), std::logic_error);
	EXPECT_THROW(seed->addUnitBonus(incoming->unitId(), {}), std::logic_error);
	EXPECT_THROW(seed->setHeroOrderStates(BattleSide::ATTACKER, {}), std::logic_error);
	EXPECT_THROW(seed->setPerfectFortuneState(BattleSide::ATTACKER, {}), std::logic_error);
	EXPECT_THROW(seed->setWallStructuralHP(EWallPart::KEEP, 1), std::logic_error);
	EXPECT_THROW(seed->removeObstacle(0), std::logic_error);
	EXPECT_EQ(incoming->acquireState()->save(), original);
	EXPECT_EQ(battle()->getRound(), nativeRound);
	ASSERT_NO_FATAL_FAILURE(startOwned());
	const auto allocated = known->nextUnitId();
	auto sourceFork = std::dynamic_pointer_cast<InitialExportBattleSubject>(
		InitialExportBattleSubject::forkForBranch(known));
	ASSERT_NE(sourceFork, nullptr);
	EXPECT_EQ(sourceFork->nextUnitId(), sourceId);
	EXPECT_EQ(sourceFork->nextUnitId(), sourceId);
	EXPECT_EQ(known->nextUnitId(), allocated + 1);
	EXPECT_EQ(known->getActualDamage(range, 10, damageRng), 15);
	known->moveUnit(incoming->unitId(), BattleHex(92));
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(92));
	EXPECT_EQ(incoming->acquireState()->save(), original);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(damageRng), damageRngBefore);
}

TEST_F(OwnedExportMeleeTest, RealOriginatingAuraRunsThroughCallbackAndOrderedNativeMeleePackets)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
	captureOwned();
	const auto record = aura(BonusDuration::UNTIL_BEING_ATTACKED, BonusType::UNLIMITED_RETALIATIONS);
	addOrigin(*incoming, record);
	startOwned();
	ASSERT_TRUE(known->battleGetUnitByID(braced->unitId())->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 2);
	const auto frozen = freezeOracle(result);
	const auto target = std::find_if(frozen.units.begin(), frozen.units.end(),
		[this](const auto & unit) { return unit.id == braced->unitId(); });
	ASSERT_NE(target, frozen.units.end());
	EXPECT_EQ(target->saved["state"]["counterAttacks"]["used"].Integer(), 0);
	EXPECT_EQ(target->counters, 1);
	EXPECT_FALSE(result.branch->battleGetUnitByID(braced->unitId())->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	retire(result);
	incoming->addNewBonus(std::make_shared<Bonus>(record));
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
	const auto & retaliation = server.attacks.back();
	ASSERT_TRUE(retaliation.counter());
	const auto actor = std::find_if(retaliation.attackerChanges.changedStacks.begin(),
		retaliation.attackerChanges.changedStacks.end(), [this](const auto & change) { return change.id == braced->unitId(); });
	ASSERT_NE(actor, retaliation.attackerChanges.changedStacks.end());
	EXPECT_EQ(actor->data["state"]["counterAttacks"]["used"].Integer(), 0);
}

TEST_F(OwnedExportMeleeTest, NonExpiringOriginAndFixedAdditionalBlowsRemainNativeComparable)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false, false, false, 1));
	const auto baseAttack = incoming->getAttack(false);
	captureOwned();
	const auto record = aura(BonusDuration::ONE_BATTLE);
	addOrigin(*incoming, record);
	startOwned();
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getAttack(false), baseAttack + 4);
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_EQ(result.originalAttackCount, 2);
	EXPECT_EQ(result.incomingBlows, 2);
	ASSERT_EQ(result.hits.size(), 2);
	EXPECT_EQ(result.hits[1].distanceArgument, 0);
	ASSERT_NO_FATAL_FAILURE(compareAfterNativeAdd(result, *incoming, record));
}

TEST_F(OwnedExportMeleeTest, WaitedBraceResourceStateUsesTheSameOwnedOriginPath)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	braced->afterWait();
	captureOwned();
	const auto record = aura(BonusDuration::UNTIL_OWN_ATTACK);
	addOrigin(*braced, record);
	startOwned();
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_TRUE(result.branch->battleGetUnitByID(braced->unitId())->acquireState()->battlecraftWaitBonusUsed);
	ASSERT_NO_FATAL_FAILURE(compareAfterNativeAdd(result, *braced, record));
}

TEST_F(OwnedExportMeleeTest, LastStandOriginatingUpdatesSurviveParentRetirementAndFork)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, true, true, true, 1));
	captureOwned();
	startOwned();
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 1);
	ASSERT_TRUE(result.hits[0].lastStandTriggered);
	const auto frozen = freezeOracle(result);
	auto continuation = std::make_shared<HypotheticBattle>(environment.get(), result.branch);
	retire(result);
	const auto * surviving = continuation->battleGetUnitByID(incoming->unitId());
	EXPECT_EQ(surviving->getAvailableHealth(), 1);
	EXPECT_TRUE(surviving->defended());
	EXPECT_TRUE(surviving->acquireState()->armorerLastStandEndedActivation);
	EXPECT_TRUE(surviving->hasBonusOfType(BonusType::UNIT_DEFENDING));
	continuation.reset();
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
}

TEST_F(OwnedExportMeleeTest, FortuneProducesAnActualOwnedPenaltyAndKeepsItsRemovalBranchLocal)
{
	captureLegacyLuckPolicy = true; // Explicit historical captured rules, not a runtime validation bypass.
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	ASSERT_TRUE(battle()->getMagicRules()["heroLuck"].isNull());
	ASSERT_TRUE(battle()->getMagicRules()["heroMorale"].isNull());
	battle()->getSide(BattleSide::DEFENDER).sylvanLuck.gambler = true;
	ASSERT_NO_THROW(battle()->validateCapturedGamblerPolicy());
	battle()->luckRollRules.diceSize = 100;
	battle()->luckRollRules.goodChance = std::vector<int>(10, 0);
	battle()->luckRollRules.badChance = std::vector<int>(10, 0);
	captureOwned();
	startOwned();
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_TRUE(result.branch->battleGetUnitByID(incoming->unitId())->hasBonus(
		CSelector(newHorizonsCombatSkills::isGamblerLuckPenalty)));
	const auto frozen = freezeOracle(result);
	auto left = std::make_shared<HypotheticBattle>(environment.get(), result.branch);
	auto right = std::make_shared<HypotheticBattle>(environment.get(), result.branch);
	left->getForUpdate(incoming->unitId())->removeGamblerLuckPenalty();
	EXPECT_FALSE(left->battleGetUnitByID(incoming->unitId())->hasBonus(CSelector(newHorizonsCombatSkills::isGamblerLuckPenalty)));
	EXPECT_TRUE(right->battleGetUnitByID(incoming->unitId())->hasBonus(CSelector(newHorizonsCombatSkills::isGamblerLuckPenalty)));
	retire(result);
	left.reset();
	right.reset();
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
}

TEST_F(OwnedExportMeleeTest, SiblingScalarsAndPropagatedOriginEditsStayInTheirActualBranch)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	captureOwned();
	startOwned();
	auto left = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto right = std::make_shared<HypotheticBattle>(environment.get(), known);
	const int before = right->battleGetUnitByID(braced->unitId())->getAttack(false);
	const auto record = aura(BonusDuration::ONE_BATTLE);
	left->getForUpdate(incoming->unitId())->addUnitBonus({record});
	left->getForUpdate(incoming->unitId())->position = BattleHex(90);
	EXPECT_EQ(left->battleGetUnitByID(braced->unitId())->getAttack(false), before + 4);
	EXPECT_EQ(right->battleGetUnitByID(braced->unitId())->getAttack(false), before);
	EXPECT_EQ(incoming->getPosition(), BattleHex(89));
	EXPECT_EQ(right->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(89));
	auto continuation = std::make_shared<HypotheticBattle>(environment.get(), left);
	left.reset();
	EXPECT_EQ(continuation->battleGetUnitByID(braced->unitId())->getAttack(false), before + 4);
	EXPECT_EQ(continuation->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(90));
}
