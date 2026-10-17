/*
 * MeleeExpiryOwnershipTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequenceFixture.h"
#include "../../lib/bonuses/Propagators.h"

class MeleeExpiryOwnershipTest : public OriginalMeleeSequenceTest
{
protected:
	std::shared_ptr<Bonus> speed(BonusDuration::Type duration, int value = 1)
	{
		return std::make_shared<Bonus>(duration, BonusType::STACKS_SPEED, BonusSource::OTHER, value, BonusSourceID());
	}

	void propagatedExpiry(BonusDuration::Type duration, bool brace)
	{
		ASSERT_NO_FATAL_FAILURE(prepareSequence(brace, false, false, false, 1));
		releaseKnown();
		auto * companion = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(72), 20);
		const int baseRange = companion->getMovementRange(0);
		auto aura = speed(duration, 2);
		aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::ARMY);
		incoming->addNewBonus(aura);
		ASSERT_TRUE(vstd::contains(*companion->getUnstackedBonuses(Selector::all), aura));
		ASSERT_EQ(companion->getMovementRange(0), baseRange + 2);
		ASSERT_NO_FATAL_FAILURE(refreshKnown());
		auto result = project();
		ASSERT_TRUE(result.meleeSequenceComplete());
		const auto * projectedCompanion = result.branch->battleGetUnitByID(companion->unitId());
		const int predictedRange = projectedCompanion->getMovementRange(0);
		const bool retained = vstd::contains(*projectedCompanion->getUnstackedBonuses(Selector::all), aura);
		EXPECT_FALSE(retained);
		EXPECT_EQ(predictedRange, baseRange);
		const auto frozen = freezeOracle(result);
		retireKnown(result);
		ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
		EXPECT_FALSE(vstd::contains(incoming->getExportedBonusList(), aura));
		EXPECT_FALSE(vstd::contains(*companion->getUnstackedBonuses(Selector::all), aura));
		EXPECT_EQ(companion->getMovementRange(0), predictedRange);
	}
};

TEST_F(MeleeExpiryOwnershipTest, HeroAncestorUntilAttackPrimaryAttackSurvivesBothOriginalIncomingBlows)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false, false, false, 1));
	releaseKnown();
	auto inherited = std::make_shared<Bonus>(BonusDuration::UNTIL_ATTACK, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 17, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	defenderSideHero->addNewBonus(inherited);
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.originalAttackCount, 2);
	ASSERT_EQ(result.incomingBlows, 2);
	ASSERT_EQ(result.hits.size(), 2);
	EXPECT_EQ(result.hits[0].actorId, incoming->unitId());
	EXPECT_EQ(result.hits[1].actorId, incoming->unitId());
	const auto frozen = freezeOracle(result);
	ASSERT_TRUE(vstd::contains(defenderSideHero->getExportedBonusList(), inherited));
	retireKnown(result);
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
	EXPECT_TRUE(vstd::contains(defenderSideHero->getExportedBonusList(), inherited));
}

TEST_F(MeleeExpiryOwnershipTest, AllFourAncestorLifetimesRetainTheirExportedIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, false, false, false, 1));
	releaseKnown();
	std::vector<std::shared_ptr<Bonus>> inherited;
	for(auto duration : {BonusDuration::UNTIL_ATTACK, BonusDuration::UNTIL_OWN_ATTACK,
		BonusDuration::UNTIL_BEING_ATTACKED, BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE})
	{
		inherited.push_back(speed(duration));
		defenderSideHero->addNewBonus(inherited.back());
	}
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto input = known->getForUpdate(incoming->unitId());
	ASSERT_TRUE(input->usesCanonicalMovementQuery(*known));
	ASSERT_FALSE(known->initialExportSubject()->world().hasUnresolvedRecipientOverlays());
	ASSERT_TRUE(std::ranges::equal(*input->getAllBonuses(Selector::all),
		*incoming->getAllBonuses(Selector::all))); // Ordered shared-pointer identity AND multiplicity.
	const auto parent = input->acquireState()->save();
	const auto parentBonuses = input->getAllBonuses(Selector::all);
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto nativeRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	known->initialExportSubject()->captureBranchValues(*known);
	auto sibling = known->initialExportSubject()->world().fork();
	const auto index = known->initialExportSubject()->occurrence(incoming->unitId());
	const auto siblingBonuses = sibling->unitAt(index).getAllBonuses(Selector::all);
	const auto siblingState = sibling->snapshotAt(index);
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete())
		<< "stop=" << static_cast<int>(result.stop)
		<< " branch=" << static_cast<bool>(result.branch)
		<< " entry=" << result.incomingEntry.has_value()
		<< " count=" << result.originalAttackCount << " hits=" << result.hits.size();
	EXPECT_EQ(input->acquireState()->save(), parent);
	EXPECT_TRUE(std::ranges::equal(*input->getAllBonuses(Selector::all), *parentBonuses));
	EXPECT_TRUE(std::ranges::equal(*sibling->unitAt(index).getAllBonuses(Selector::all), *siblingBonuses));
	EXPECT_EQ(sibling->snapshotAt(index), siblingState);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), nativeRng);
	sibling.reset();
	input.reset();
	const auto * projected = result.branch->battleGetUnitByID(incoming->unitId());
	for(const auto & record : inherited)
		EXPECT_TRUE(vstd::contains(*projected->getUnstackedBonuses(Selector::all), record));
	const int predictedRange = projected->getMovementRange(0);
	const auto frozen = freezeOracle(result);
	retireKnown(result);
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
	for(const auto & record : inherited)
	{
		EXPECT_TRUE(vstd::contains(defenderSideHero->getExportedBonusList(), record));
		EXPECT_TRUE(vstd::contains(*incoming->getUnstackedBonuses(Selector::all), record));
	}
	EXPECT_EQ(incoming->getMovementRange(0), predictedRange);
}

TEST_F(MeleeExpiryOwnershipTest, EqualLocalAndAncestorValuesExpireOnlyTheExportOwnerClosure)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false, false, false, 1));
	releaseKnown();
	const int baseRange = incoming->getMovementRange(0);
	auto inherited = speed(BonusDuration::UNTIL_ATTACK), local = speed(BonusDuration::UNTIL_ATTACK);
	defenderSideHero->addNewBonus(inherited);
	incoming->addNewBonus(local);
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	const auto * projected = result.branch->battleGetUnitByID(incoming->unitId());
	EXPECT_TRUE(vstd::contains(*projected->getUnstackedBonuses(Selector::all), inherited));
	EXPECT_FALSE(vstd::contains(*projected->getUnstackedBonuses(Selector::all), local));
	EXPECT_EQ(projected->getMovementRange(0), baseRange + 1);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
	EXPECT_TRUE(vstd::contains(defenderSideHero->getExportedBonusList(), inherited));
	EXPECT_FALSE(vstd::contains(incoming->getExportedBonusList(), local));
	EXPECT_EQ(incoming->getMovementRange(0), baseRange + 1);
}

TEST_F(MeleeExpiryOwnershipTest, UntilAttackUnpropagatesTheOwnedAuraFromAnotherRosterParticipant)
{
	ASSERT_NO_FATAL_FAILURE(propagatedExpiry(BonusDuration::UNTIL_ATTACK, false));
}

TEST_F(MeleeExpiryOwnershipTest, UntilBeingAttackedUnpropagatesTheOwnedAuraBeforeTheIncomingHit)
{
	ASSERT_NO_FATAL_FAILURE(propagatedExpiry(BonusDuration::UNTIL_BEING_ATTACKED, true));
}

TEST_F(MeleeExpiryOwnershipTest, UntilOwnAttackUnpropagatesTheOwnedAuraFromAnotherRosterParticipant)
{
	ASSERT_NO_FATAL_FAILURE(propagatedExpiry(BonusDuration::UNTIL_OWN_ATTACK, false));
}

TEST_F(MeleeExpiryOwnershipTest, SequenceEndUnpropagatesOnlyTheOriginallySelectedOwnerAura)
{
	ASSERT_NO_FATAL_FAILURE(propagatedExpiry(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE, false));
}

TEST_F(MeleeExpiryOwnershipTest, FrozenOracleSurvivesNativeSourceMutationAndProjectedLocalExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false, false, false, 1));
	releaseKnown();
	auto inherited = speed(BonusDuration::UNTIL_ATTACK);
	defenderSideHero->addNewBonus(inherited);
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	const auto local = speed(BonusDuration::UNTIL_ATTACK, 2);
	known->getForUpdate(incoming->unitId())->addUnitBonus({*local});
	auto input = known->getForUpdate(incoming->unitId());
	ASSERT_TRUE(input->usesCanonicalMovementQuery(*known));
	ASSERT_FALSE(known->initialExportSubject()->world().hasUnresolvedRecipientOverlays());
	const auto parent = input->acquireState()->save();
	const auto parentBonuses = input->getAllBonuses(Selector::all);
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto nativeRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	known->initialExportSubject()->captureBranchValues(*known);
	auto sibling = known->initialExportSubject()->world().fork();
	const auto index = known->initialExportSubject()->occurrence(incoming->unitId());
	const auto siblingBonuses = sibling->unitAt(index).getAllBonuses(Selector::all);
	const auto siblingState = sibling->snapshotAt(index);
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete())
		<< "stop=" << static_cast<int>(result.stop)
		<< " branch=" << static_cast<bool>(result.branch)
		<< " entry=" << result.incomingEntry.has_value()
		<< " count=" << result.originalAttackCount << " hits=" << result.hits.size();
	EXPECT_EQ(input->acquireState()->save(), parent);
	EXPECT_TRUE(std::ranges::equal(*input->getAllBonuses(Selector::all), *parentBonuses));
	EXPECT_TRUE(std::ranges::equal(*sibling->unitAt(index).getAllBonuses(Selector::all), *siblingBonuses));
	EXPECT_EQ(sibling->snapshotAt(index), siblingState);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), nativeRng);
	sibling.reset();
	input.reset();
	const auto frozen = freezeOracle(result);
	const auto saved = frozen.units.front().saved.toString();
	const auto receipts = frozen.hits;
	retireKnown(result);
	// Align the authoritative input with the prior detached ADD only AFTER all
	// projected oracles are owned. Never query the now-invalid borrowed branch.
	incoming->addNewBonus(local);
	EXPECT_EQ(frozen.units.front().saved.toString(), saved);
	EXPECT_EQ(frozen.hits, receipts);
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
	EXPECT_TRUE(vstd::contains(defenderSideHero->getExportedBonusList(), inherited));
	EXPECT_FALSE(vstd::contains(incoming->getExportedBonusList(), local));
}
