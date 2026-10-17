/*
 * OriginalMeleeSequenceTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/OriginalMeleeSequence.h"
#include "../../AI/BattleAI/HeroActionUtility.h"
#include "../../lib/CRandomGenerator.h"

#include <type_traits>

static_assert(!std::is_convertible_v<heroOrderValue::OriginalMeleeSequence, float>);
static_assert(!std::is_convertible_v<heroOrderValue::OriginalMeleeSequence, heroActionUtility::AbsoluteHeroActionUtility>);

#include "OriginalMeleeSequenceFixture.h"

TEST_F(OriginalMeleeSequenceTest, NoBraceAndBraceUseTheSameOrdinaryIncomingAndRetaliationStages)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false));
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 2);
	EXPECT_EQ(result.hits.front().kind, heroOrderValue::MeleeStrikeKind::INCOMING_PRIMARY);
	EXPECT_EQ(result.hits.back().kind, heroOrderValue::MeleeStrikeKind::ORDINARY_RETALIATION);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
}

TEST_F(OriginalMeleeSequenceTest, BraceActuallyAdvancesThroughIncomingAndOneOrdinaryRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 3);
	EXPECT_EQ(result.hits[0].kind, heroOrderValue::MeleeStrikeKind::BRACE_PRIMARY);
	EXPECT_FALSE(result.hits[0].damageRetaliation);
	EXPECT_TRUE(result.hits[0].reactiveForLastStand);
	EXPECT_EQ(result.hits[1].actorHealthBefore, result.hits[0].victimHealthAfter);
	EXPECT_EQ(result.hits[1].distanceArgument, 3);
	ASSERT_TRUE(result.incomingEntry);
	EXPECT_EQ(result.incomingEntry->voluntaryMovementDistance, 3);
	EXPECT_EQ(result.hits[1].selectedTargetHex, action().targetHex);
	EXPECT_EQ(result.hits[2].kind, heroOrderValue::MeleeStrikeKind::ORDINARY_RETALIATION);
	EXPECT_FALSE(result.activationComplete);
	EXPECT_FALSE(result.absoluteHorizonComplete);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
}

TEST_F(OriginalMeleeSequenceTest, AdditionalIncomingBlowKeepsOriginalCountAndHasZeroSecondDistance)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, true, false, false, 1, 200));
	// Count/index assertions require surviving the first native reaction.
	ASSERT_EQ(incoming->getAvailableHealth(), 2000);
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	// Freeze every native-comparison value before retiring the borrowed
	// branch. Compare the actual server sequence before a fatal expected
	// hit-count oracle can prevent that evidence from being collected.
	ASSERT_TRUE(result.branch);
	const auto frozen = freezeOracle(result);
	retireKnown(result);
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
	EXPECT_EQ(result.originalAttackCount, 2);
	EXPECT_EQ(result.incomingBlows, 2) << "stop=" << static_cast<int>(result.stop)
		<< " hits=" << result.hits.size()
		<< " actorAfter=" << frozen.units.front().health
		<< " targetAfter=" << frozen.units.back().health;
	ASSERT_EQ(result.hits.size(), 4);
	EXPECT_EQ(result.hits.back().kind, heroOrderValue::MeleeStrikeKind::INCOMING_PRIMARY);
	EXPECT_EQ(result.hits.back().attackIndex, 1);
	EXPECT_EQ(result.hits.back().distanceArgument, 0);
}

TEST_F(OriginalMeleeSequenceTest, BlockedOrdinaryRetaliationNeverSpendsTheBracedCounterResource)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, false));
	const auto counters = braced->counterAttacks.available();
	auto result = project();
	ASSERT_EQ(result.hits.size(), 2);
	EXPECT_EQ(result.branch->battleGetUnitByID(braced->unitId())->acquireState()->counterAttacks.available(), counters);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
}

TEST_F(OriginalMeleeSequenceTest, LethalBraceFinishesSupportedPhysicalSequenceWithoutInventingIncoming)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, true, false, true, 1));
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_EQ(result.originalAttackCount, 2);
	EXPECT_EQ(result.incomingBlows, 0);
	ASSERT_EQ(result.hits.size(), 1);
	EXPECT_EQ(result.hits.front().victimHealthAfter, 0);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
}

TEST_F(OriginalMeleeSequenceTest, ReactiveLastStandFinishesSupportedSequenceWithoutAnyIncomingBlow)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, true, true, true, 1));
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 1);
	EXPECT_TRUE(result.hits.front().lastStandTriggered);
	EXPECT_EQ(result.hits.front().victimHealthAfter, 1);
	EXPECT_EQ(result.incomingBlows, 0);
	EXPECT_TRUE(result.branch->battleGetUnitByID(incoming->unitId())->acquireState()->armorerLastStandEndedActivation);
	const bool projectedDefended = result.branch->battleGetUnitByID(incoming->unitId())->defended();
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
	EXPECT_EQ(projectedDefended, incoming->defended());
}

TEST_F(OriginalMeleeSequenceTest, BraceExpiryCannotRecomputeAwayTheOriginalFixedIncomingLoopCount)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, true, false, false, 0, 200));
	releaseKnown();
	incoming->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_BEING_ATTACKED,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(incoming->getTotalAttacks(false), 2);
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	// Count/index assertions require surviving the first native reaction.
	ASSERT_EQ(incoming->getAvailableHealth(), 2000);
	auto result = project();
	EXPECT_EQ(result.originalAttackCount, 2);
	EXPECT_EQ(result.incomingBlows, 2) << "stop=" << static_cast<int>(result.stop)
		<< " hits=" << result.hits.size();
	ASSERT_TRUE(result.branch);
	const auto projectedAttacksAfterExpiry = result.branch->battleGetUnitByID(incoming->unitId())->getTotalAttacks(false);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
	EXPECT_EQ(projectedAttacksAfterExpiry, 1);
}

TEST_F(OriginalMeleeSequenceTest, BraceSpendsOwnAttackExpiryButNeverBecomesAFullCreatureExtraAttackLoop)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	releaseKnown();
	braced->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_OWN_ATTACK,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_EQ(std::ranges::count(result.hits, heroOrderValue::MeleeStrikeKind::BRACE_PRIMARY,
		&heroOrderValue::MeleeHitReceipt::kind), 1);
	EXPECT_EQ(result.branch->battleGetUnitByID(braced->unitId())->getTotalAttacks(false), 1);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
}

TEST_F(OriginalMeleeSequenceTest, BraceAndRetaliationUseTheNativePhysicalWaitResourceBoundary)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	releaseKnown();
	braced->afterWait();
	ASSERT_TRUE(braced->waitedThisTurn);
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_TRUE(result.branch->battleGetUnitByID(braced->unitId())->acquireState()->battlecraftWaitBonusUsed);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
}

TEST_F(OriginalMeleeSequenceTest, RebirthIsPendingBeforeInjuryRatherThanSampledOrDeclaredFinalSuppression)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, false, false, true));
	releaseKnown();
	incoming->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
	incoming->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto result = project();
	EXPECT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::REBIRTH_PENDING);
	EXPECT_FALSE(result.meleeSequenceComplete());
	EXPECT_TRUE(result.hits.empty());
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(OriginalMeleeSequenceTest, SequenceExpiryUsesBothOriginalParticipantsAndNeverFinishesAnActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	const auto actorRange = incoming->getMovementRange(0), targetRange = braced->getMovementRange(0);
	releaseKnown();
	for(CStack * unit : {incoming, braced})
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE,
			BonusType::STACKS_SPEED, BonusSource::OTHER, 2, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getMovementRange(0), actorRange);
	EXPECT_EQ(result.branch->battleGetUnitByID(braced->unitId())->getMovementRange(0), targetRange);
	EXPECT_FALSE(result.activationComplete);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
	EXPECT_EQ(incoming->getMovementRange(0), actorRange);
	EXPECT_EQ(braced->getMovementRange(0), targetRange);
}

TEST_F(OriginalMeleeSequenceTest, UnknownLuckLeavesOnlyMovementAndNeverSamplesAHitOrCompletesTheHorizon)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	releaseKnown();
	braced->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto result = project();
	EXPECT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::UNKNOWN_LUCK_PENDING);
	EXPECT_FALSE(result.meleeSequenceComplete());
	EXPECT_TRUE(result.hits.empty());
	ASSERT_TRUE(result.branch);
	EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(92));
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(89));
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(OriginalMeleeSequenceTest, NoQuarterNeedsItsOrderedDenialOwnerBeforeAnyPrimaryInjury)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	releaseKnown();
	const int offense = SecondarySkill::decode("new-horizons:offense");
	ASSERT_GE(offense, 0);
	const std::vector<std::string> perks{"new-horizons:offense.shockAssault", "new-horizons:offense.armorPiercer", "new-horizons:offense.noQuarter"};
	for(size_t index = 0; index < perks.size(); ++index)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(offense), index + 1, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk("new-horizons:offense", perks[index],
			attackerSideHero->getPerkSkillRank("new-horizons:offense")));
		attackerSideHero->applyPerkSelection({"new-horizons:offense", perks[index]});
	}
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto result = project();
	EXPECT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::NO_QUARTER_OR_DEFIANT_PENDING);
	EXPECT_FALSE(result.meleeSequenceComplete());
	EXPECT_TRUE(result.hits.empty());
	EXPECT_FALSE(result.branch);
}

TEST_F(OriginalMeleeSequenceTest, DefiantIsNotAssumedConsumedOrInapplicableBeforeTheRetaliationGate)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	releaseKnown();
	ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::BASIC, "new-horizons:armorer.pavise"));
	ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::ADVANCED, "new-horizons:armorer.defiant"));
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto result = project();
	EXPECT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::NO_QUARTER_OR_DEFIANT_PENDING);
	EXPECT_FALSE(result.meleeSequenceComplete());
	EXPECT_TRUE(result.hits.empty());
	EXPECT_EQ(result.branch, nullptr);
}

TEST_F(OriginalMeleeSequenceTest, HiddenPlayerHeroPolicyIsNotTreatedAsAnAbsentHero)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	resetChild();
	const auto result = heroOrderValue::projectOriginalMeleeSequence(environment.get(), child, action());
	EXPECT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::HIDDEN_HERO_POLICY);
	EXPECT_FALSE(result.meleeSequenceComplete());
	EXPECT_FALSE(result.branch);
	EXPECT_TRUE(result.hits.empty());
}

TEST_F(OriginalMeleeSequenceTest, CompleteSupportedSequenceLeavesLiveParentSiblingLedgersAndBothRngsUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
	const auto source = known->battleGetUnitByID(incoming->unitId())->acquireState()->save().toString();
	const auto original = incoming->acquireState()->save().toString();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto nativeRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto allowances = projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER));
	const auto orders = projectedHitTest::serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER));
	const auto traffic = server.startedActions.size();
	const auto first = project(), second = project();
	ASSERT_TRUE(first.meleeSequenceComplete());
	EXPECT_EQ(first.hits, second.hits);
	for(const auto & unchanged : {known, sibling})
		EXPECT_EQ(unchanged->battleGetUnitByID(incoming->unitId())->acquireState()->save().toString(), source);
	EXPECT_EQ(incoming->acquireState()->save().toString(), original);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), nativeRng);
	EXPECT_EQ(projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER)), allowances);
	EXPECT_EQ(projectedHitTest::serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER)), orders);
	EXPECT_EQ(server.startedActions.size(), traffic);
}
