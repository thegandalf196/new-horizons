/*
 * RecipientMeleeInjuryTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequenceFixture.h"
#include "../../AI/BattleAI/ProjectedMeleeExpiry.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/battle/StackEffectRemoval.h"
#include "../../lib/spells/CSpell.h"

class RecipientMeleeInjuryTest : public OriginalMeleeSequenceTest
{
protected:
	void prepareLegacy()
	{
		ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
		ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	}
	Bonus rating(bool propagated = true)
	{
		Bonus value(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::OTHER,
			4, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
		if(propagated)
			value.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
		value.limiter = std::make_shared<CreatureLevelLimiter>();
		return value;
	}
	heroOrderValue::OriginalMeleeSequence continued()
	{
		return heroOrderValue::projectLegacyRecipientMeleeSequence(environment.get(), known, *battle(), action());
	}
	void oldPrefixRemainsExplicit()
	{
		auto old = project();
		EXPECT_EQ(old.stop, heroOrderValue::MeleeSequenceStop::UNSUPPORTED_REACTION_POLICY);
		ASSERT_TRUE(old.branch);
		ASSERT_TRUE(old.incomingEntry);
		EXPECT_TRUE(old.hits.empty());
		EXPECT_EQ(old.incomingEntry->voluntaryMovementDistance, 3);
		EXPECT_EQ(old.originalAttackCount, 1);
		EXPECT_EQ(old.branch->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(92));
		EXPECT_EQ(old.branch->battleGetUnitByID(incoming->unitId())->getAvailableHealth(), incoming->getAvailableHealth());
	}
	void expectRecordedFork(heroOrderValue::OriginalMeleeSequence & result)
	{
		ASSERT_TRUE(result.meleeSequenceComplete());
		ASSERT_TRUE(result.branch);
		const auto subject = result.branch->initialExportSubject();
		ASSERT_TRUE(subject);
		subject->captureBranchValues(*result.branch);
		ASSERT_TRUE(subject->world().hasRecordedRecipientInjuryInputs());
		auto fork = subject->world().fork();
		for(const auto * original : {incoming, braced})
		{
			const auto index = subject->occurrence(original->unitId());
			EXPECT_EQ(fork->snapshotAt(index), subject->world().snapshotAt(index));
			EXPECT_EQ(fork->unitAt(index).getAttack(false), result.branch->battleGetUnitByID(original->unitId())->getAttack(false));
		}
		EXPECT_FALSE(result.activationComplete);
		EXPECT_FALSE(result.absoluteHorizonComplete);
	}
};

TEST_F(RecipientMeleeInjuryTest, RecipientOnlyPropagatorMetadataDoesNotBecomeANativeAuraAtInjury)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	const auto record = rating();
	const int attack = incoming->getAttack(false), other = braced->getAttack(false);
	known->getForUpdate(incoming->unitId())->addUnitBonus({record});
	ASSERT_NO_FATAL_FAILURE(oldPrefixRemainsExplicit());
	const auto parent = known->battleGetUnitByID(incoming->unitId())->acquireState()->save();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto result = continued();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 2);
	EXPECT_EQ(result.incomingBlows, 1);
	EXPECT_EQ(result.hits.front().kind, heroOrderValue::MeleeStrikeKind::INCOMING_PRIMARY);
	EXPECT_EQ(result.hits.back().kind, heroOrderValue::MeleeStrikeKind::ORDINARY_RETALIATION);
	EXPECT_GT(result.hits.front().appliedDamage, 0);
	EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getAttack(false), attack + 4);
	EXPECT_EQ(result.branch->battleGetUnitByID(braced->unitId())->getAttack(false), other);
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->acquireState()->save(), parent);
	EXPECT_EQ(incoming->getAttack(false), attack);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	ASSERT_NO_FATAL_FAILURE(expectRecordedFork(result));
}

TEST_F(RecipientMeleeInjuryTest, RecipientRemovalKeepsItsOtherNativeAuraRecipientAndReplay)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	releaseKnown();
	auto record = std::make_shared<Bonus>(rating());
	incoming->addNewBonus(record);
	ASSERT_TRUE(vstd::contains(*braced->getUnstackedBonuses(Selector::all), record));
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	known->getForUpdate(incoming->unitId())->removeUnitBonus(std::vector<Bonus>{*record});
	const int expected = known->battleGetUnitByID(incoming->unitId())->getAttack(false);
	ASSERT_NO_FATAL_FAILURE(oldPrefixRemainsExplicit());
	auto result = continued();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getAttack(false), expected);
	EXPECT_FALSE(vstd::contains(*result.branch->battleGetUnitByID(incoming->unitId())->getUnstackedBonuses(Selector::all), record));
	EXPECT_TRUE(vstd::contains(*result.branch->battleGetUnitByID(braced->unitId())->getUnstackedBonuses(Selector::all), record));
	EXPECT_TRUE(vstd::contains(incoming->getExportedBonusList(), record));
	ASSERT_NO_FATAL_FAILURE(expectRecordedFork(result));
}

TEST_F(RecipientMeleeInjuryTest, RecipientRefreshWithoutAnInheritedMatchDoesNotInventAnOriginOrRating)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	const int nativeAttack = incoming->getAttack(false);
	known->getForUpdate(incoming->unitId())->updateUnitBonus({rating()});
	const int expected = known->battleGetUnitByID(incoming->unitId())->getAttack(false);
	ASSERT_EQ(expected, nativeAttack + 4); // Existing unmatched UPDATE is a recipient-local addition.
	ASSERT_FALSE(known->getForUpdate(incoming->unitId())->bonusesToUpdate.empty());
	ASSERT_NO_FATAL_FAILURE(oldPrefixRemainsExplicit());
	auto result = continued();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getAttack(false), expected);
	EXPECT_EQ(incoming->getAttack(false), nativeAttack);
	ASSERT_NO_FATAL_FAILURE(expectRecordedFork(result));
}

TEST_F(RecipientMeleeInjuryTest, CapturedHeroSpecialValueAndClockSurvivePhysicalInjuryWithoutNativeWrite)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	Bonus record = rating();
	record.duration = BonusDuration::N_TURNS;
	record.source = BonusSource::HERO_SPECIAL;
	record.turnsRemain = 2;
	const int expected = incoming->getAttack(false) + 4;
	const auto layer = known->getForUpdate(incoming->unitId());
	layer->addUnitBonus({record});
	layer->removeUnitBonus(Bonus::UntilAttack);
	ASSERT_TRUE(layer->bonusesToAdd.empty());
	ASSERT_NO_FATAL_FAILURE(oldPrefixRemainsExplicit());
	auto result = continued();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getAttack(false), expected);
	const auto selector = Selector::source(record.source, record.sid).And(Selector::typeSubtype(record.type, record.subtype));
	const auto values = result.branch->battleGetUnitByID(incoming->unitId())->getUnstackedBonuses(selector);
	ASSERT_EQ(values->size(), 1);
	EXPECT_EQ(values->front()->val, 4);
	EXPECT_EQ(values->front()->turnsRemain, 2);
	EXPECT_TRUE(incoming->getUnstackedBonuses(selector)->empty());
	ASSERT_NO_FATAL_FAILURE(expectRecordedFork(result));
}

TEST_F(RecipientMeleeInjuryTest, VictimUpdateExpiresOnlyItsLocalUntilBeingAttackedAndKeepsTheSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	known->getForUpdate(incoming->unitId())->addUnitBonus({rating()});
	Bonus unlimited(BonusDuration::UNTIL_BEING_ATTACKED, BonusType::UNLIMITED_RETALIATIONS,
		BonusSource::OTHER, 1, BonusSourceID());
	known->getForUpdate(incoming->unitId())->addUnitBonus({unlimited});
	const auto imported = InitialExportBattleSubject::captureLegacyMovement(known, *battle());
	auto sibling = imported->world().fork();
	const auto index = imported->occurrence(incoming->unitId());
	ASSERT_TRUE(sibling->unitAt(index).hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	auto result = continued();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 2);
	EXPECT_GT(result.hits.back().appliedDamage, 0);
	EXPECT_FALSE(result.branch->battleGetUnitByID(incoming->unitId())->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	EXPECT_TRUE(sibling->unitAt(index).hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	EXPECT_TRUE(known->battleGetUnitByID(incoming->unitId())->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	EXPECT_FALSE(incoming->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	ASSERT_NO_FATAL_FAILURE(expectRecordedFork(result));
}

TEST_F(RecipientMeleeInjuryTest, UnitLocalHistoryMatchesActualNativePacketsAndPreVictimCounterSpend)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	releaseKnown();
	auto unlimited = std::make_shared<Bonus>(BonusDuration::UNTIL_BEING_ATTACKED,
		BonusType::UNLIMITED_RETALIATIONS, BonusSource::OTHER, 1, BonusSourceID());
	unlimited->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	incoming->addNewBonus(unlimited);
	ASSERT_TRUE(vstd::contains(*braced->getUnstackedBonuses(Selector::all), unlimited));
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	const auto local = rating(false); // Real native counterpart is unit-local too.
	known->getForUpdate(incoming->unitId())->addUnitBonus({local});
	auto result = continued();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 2);
	ASSERT_NO_FATAL_FAILURE(expectRecordedFork(result));
	const auto frozen = freezeOracle(result); // Freeze EVERY borrowed scalar/save before dispatch.
	const auto victim = std::find_if(frozen.units.begin(), frozen.units.end(),
		[this](const auto & unit) { return unit.id == braced->unitId(); });
	ASSERT_NE(victim, frozen.units.end());
	EXPECT_EQ(victim->saved["state"]["counterAttacks"]["used"].Integer(), 0);
	retireKnown(result);
	incoming->addNewBonus(std::make_shared<Bonus>(local));
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
}

TEST_F(RecipientMeleeInjuryTest, UnrecordedProgramInputsCannotBeRebrandedAsAnOwnedInjurySource)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	known->getForUpdate(incoming->unitId())->addUnitBonus({rating()});
	const auto imported = InitialExportBattleSubject::captureLegacyMovement(known, *battle());
	const auto source = imported->world().sourceAt(imported->occurrence(incoming->unitId()));
	imported->world().copyQueryValues(source, *known->getForUpdate(incoming->unitId()));
	ASSERT_FALSE(imported->world().hasRecordedRecipientInjuryInputs());
	const auto dirtyFork = imported->world().fork();
	EXPECT_FALSE(dirtyFork->hasRecordedRecipientInjuryInputs());
	int64_t damage = 1;
	EXPECT_THROW(dirtyFork->snapshotAfterPhysicalDamage(source, damage), std::logic_error);
	imported->world().removeMatching(source, Selector::type()(BonusType::LUCK));
	imported->world().recordQuerySnapshot(source);
	EXPECT_FALSE(imported->world().hasRecordedRecipientInjuryInputs());
	const auto replay = imported->world().fork();
	EXPECT_FALSE(replay->hasRecordedRecipientInjuryInputs());
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), imported);
	ProjectedMeleeExpiry expiry;
	EXPECT_FALSE(expiry.captureRecipientLayers(*branch));
	EXPECT_EQ(incoming->getAvailableHealth(), known->battleGetUnitByID(incoming->unitId())->getAvailableHealth());
}

TEST_F(RecipientMeleeInjuryTest, DeathUpdateUsesTheSharedTransientPredicateForRecipientLayersAndReplay)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	const SpellID haste(SpellID::HASTE), ray(SpellID::DISRUPTING_RAY);
	ASSERT_FALSE(haste.toSpell()->isPersistent());
	ASSERT_TRUE(ray.toSpell()->isPersistent());
	Bonus transient(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT,
		1, BonusSourceID(haste), BonusSubtypeID(PrimarySkill::ATTACK));
	transient.turnsRemain = 2;
	Bonus persistent(transient);
	persistent.sid = BonusSourceID(ray);
	known->getForUpdate(incoming->unitId())->addUnitBonus({transient, persistent});
	const auto imported = InitialExportBattleSubject::captureLegacyMovement(known, *battle());
	const auto source = imported->world().sourceAt(imported->occurrence(incoming->unitId()));
	const auto before = imported->world().snapshotAt(source.occurrence);
	auto damage = imported->world().unitAt(source.occurrence).getAvailableHealth();
	const auto after = imported->world().snapshotAfterPhysicalDamage(source, damage);
	ASSERT_EQ(imported->world().updateUnitFrom(source, before, after, -damage),
		battle::ProjectedBattleWorldArena::UpdateDecision::APPLY);
	const auto replay = imported->world().fork();
	for(const auto * owner : {&imported->world(), replay.get()})
	{
		EXPECT_FALSE(owner->unitAt(source.occurrence).alive());
		EXPECT_TRUE(owner->unitAt(source.occurrence).getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(haste)))->empty());
		EXPECT_FALSE(owner->unitAt(source.occurrence).getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(ray)))->empty());
	}
	EXPECT_TRUE(incoming->alive());
	EXPECT_TRUE(known->battleGetUnitByID(incoming->unitId())->alive());
}

TEST_F(RecipientMeleeInjuryTest, EmptyHistoryKeepsTheExistingCanonicalSequenceAndNativePackets)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	auto original = project();
	ASSERT_TRUE(original.meleeSequenceComplete());
	auto result = continued();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_EQ(original.hits, result.hits);
	original.branch.reset(); // No comparison provider may survive native dispatch.
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
}
