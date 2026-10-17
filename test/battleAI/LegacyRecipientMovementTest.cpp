/*
 * LegacyRecipientMovementTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequenceFixture.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/PreparedBonusGraphFinalization.h"
#include "../../lib/bonuses/Propagators.h"

class LegacyRecipientMovementTest : public OriginalMeleeSequenceTest
{
protected:
	void prepareLegacy()
	{
		ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
		ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	}
	Bonus localAttack(int amount)
	{
		return Bonus(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::OTHER,
			amount, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	}
};

TEST_F(LegacyRecipientMovementTest, NativePositionLimiterAndRecipientOnlyAddReachTheRealInjuryBoundary)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	releaseKnown();
	auto native = std::make_shared<Bonus>(localAttack(3));
	native->limiter = std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(92)});
	incoming->addNewBonus(native);
	const int before = incoming->getAttack(false);
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	auto local = localAttack(4);
	local.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	known->getForUpdate(incoming->unitId())->addUnitBonus({local});
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto result = project();
	ASSERT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::UNSUPPORTED_REACTION_POLICY);
	ASSERT_TRUE(result.branch);
	ASSERT_TRUE(result.incomingEntry);
	EXPECT_EQ(result.incomingEntry->voluntaryMovementDistance, 3);
	EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(92));
	EXPECT_EQ(result.originalAttackCount, 1);
	EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getAttack(false), before + 7);
	EXPECT_EQ(result.branch->battleGetUnitByID(braced->unitId())->getAttack(false), braced->getAttack(false));
	EXPECT_TRUE(result.hits.empty());
	EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getAvailableHealth(), incoming->getAvailableHealth());
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(89));
	EXPECT_EQ(incoming->getAttack(false), before);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(LegacyRecipientMovementTest, GraphBoundHealthSaveAndForkUseTheSameRecipientLayers)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	Bonus health(BonusDuration::ONE_BATTLE, BonusType::STACK_HEALTH, BonusSource::OTHER,
		2, BonusSourceID());
	known->getForUpdate(incoming->unitId())->addUnitBonus({health});
	const auto side = incoming->unitSide();
	ASSERT_EQ(battle()->getSylvanLuckState(side).speedBonus(incoming->unitId()), 0);
	const auto nativeMovement = incoming->getMovementRange();
	auto fortune = known->getSylvanLuckState(side);
	fortune.forestsFavor = true;
	fortune.positiveLuckUnits.insert(incoming->unitId());
	fortune.speedUnits.insert(incoming->unitId());
	known->setSylvanLuckState(side, fortune); // Private environment differs from native.
	const auto * originalQuery = known->battleGetUnitByID(incoming->unitId());
	const auto movement = originalQuery->getMovementRange();
	ASSERT_EQ(movement, nativeMovement + 2);
	const auto expected = originalQuery->acquireState()->save();
	const int maxHealth = originalQuery->getMaxHealth();
	const auto parent = std::weak_ptr<HypotheticBattle>(known);
	auto imported = InitialExportBattleSubject::captureLegacyMovement(known, *battle());
	const auto index = imported->occurrence(incoming->unitId());
	EXPECT_EQ(imported->world().unitAt(index).getMaxHealth(), maxHealth);
	EXPECT_EQ(imported->world().unitAt(index).getMovementRange(), movement);
	EXPECT_EQ(imported->world().snapshotAt(index), expected);
	auto fork = imported->world().fork();
	known.reset();
	EXPECT_FALSE(parent.expired()); // Actual source ownership, not a synthetic lease.
	imported.reset();
	EXPECT_TRUE(parent.expired());
	EXPECT_EQ(fork->unitAt(index).getMaxHealth(), maxHealth);
	EXPECT_EQ(fork->unitAt(index).getMovementRange(), movement);
	EXPECT_EQ(fork->snapshotAt(index), expected);
	EXPECT_EQ(incoming->getMaxHealth(), maxHealth - 2);
	EXPECT_EQ(incoming->getMovementRange(), nativeMovement);
	EXPECT_EQ(battle()->getSylvanLuckState(side).speedBonus(incoming->unitId()), 0);
}

TEST_F(LegacyRecipientMovementTest, RefreshKeepsOriginalValueAndPrivateClockWithoutEditingNativePointee)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	releaseKnown();
	auto native = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL,
		BonusSource::SPELL_EFFECT, 7, BonusSourceID(SpellID(SpellID::HASTE)), BonusSubtypeID(PrimarySkill::ATTACK));
	native->turnsRemain = 2;
	incoming->addNewBonus(native);
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	Bonus update(*native);
	update.val = 99;
	update.turnsRemain = 5;
	known->getForUpdate(incoming->unitId())->updateUnitBonus({update});
	const auto selector = Selector::source(native->source, native->sid)
		.And(Selector::typeSubtypeValueType(native->type, native->subtype, native->valType));
	const auto legacy = known->battleGetUnitByID(incoming->unitId())->getUnstackedBonuses(selector);
	ASSERT_EQ(legacy->size(), 1);
	ASSERT_EQ(legacy->front()->val, 7);
	ASSERT_EQ(legacy->front()->turnsRemain, 5);
	const auto imported = InitialExportBattleSubject::captureLegacyMovement(known, *battle());
	const auto actual = imported->world().unitAt(imported->occurrence(incoming->unitId())).getUnstackedBonuses(selector);
	ASSERT_EQ(actual->size(), 1);
	EXPECT_EQ(actual->front()->val, legacy->front()->val);
	EXPECT_EQ(actual->front()->turnsRemain, legacy->front()->turnsRemain);
	EXPECT_EQ(native->val, 7);
	EXPECT_EQ(native->turnsRemain, 2);
}

TEST_F(LegacyRecipientMovementTest, EmptyLegacyLayersStillMatchTheActualNativePhysicalSequence)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 2);
	EXPECT_EQ(result.hits.front().kind, heroOrderValue::MeleeStrikeKind::INCOMING_PRIMARY);
	EXPECT_EQ(result.hits.back().kind, heroOrderValue::MeleeStrikeKind::ORDINARY_RETALIATION);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
}

TEST_F(LegacyRecipientMovementTest, HiddenSourceIsNotImportedAsAnAuthoritativeWorld)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	const auto hidden = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	EXPECT_THROW(InitialExportBattleSubject::captureLegacyMovement(hidden, *battle()), std::invalid_argument);
	const auto result = heroOrderValue::projectLegacyOriginalMeleeSequence(environment.get(), hidden, *battle(), action());
	EXPECT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::HIDDEN_HERO_POLICY);
	EXPECT_FALSE(result.branch);
}

TEST_F(LegacyRecipientMovementTest, MovementRemovesRecipientEntangleWithoutInventingAnExportOrChangingSiblings)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	const SpellID entangle(SpellID::decode("new-horizons:entangle"));
	ASSERT_TRUE(entangle.hasValue());
	Bonus marker(BonusDuration::ONE_BATTLE, BonusType::BIND_EFFECT,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(entangle));
	marker.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	known->getForUpdate(incoming->unitId())->addUnitBonus({marker});
	ASSERT_TRUE(known->battleGetUnitByID(incoming->unitId())->hasBonusOfType(BonusType::BIND_EFFECT));
	ASSERT_FALSE(defenderSideHero->hasBonusOfType(BonusType::BIND_EFFECT));
	auto imported = InitialExportBattleSubject::captureLegacyMovement(known, *battle());
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), imported);
	const auto before = branch->initialExportSubject()->world().fork();
	const auto index = imported->occurrence(incoming->unitId());
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	branch->moveUnit(incoming->unitId(), BattleHex(90));
	EXPECT_FALSE(branch->battleGetUnitByID(incoming->unitId())->hasBonusOfType(BonusType::BIND_EFFECT));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), branch);
	EXPECT_FALSE(child->battleGetUnitByID(incoming->unitId())->hasBonusOfType(BonusType::BIND_EFFECT));
	EXPECT_TRUE(before->unitAt(index).hasBonusOfType(BonusType::BIND_EFFECT));
	EXPECT_TRUE(known->battleGetUnitByID(incoming->unitId())->hasBonusOfType(BonusType::BIND_EFFECT));
	EXPECT_FALSE(defenderSideHero->hasBonusOfType(BonusType::BIND_EFFECT));
	EXPECT_EQ(incoming->getPosition(), BattleHex(89));
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(LegacyRecipientMovementTest, QueryLayersCannotBePublishedAsAnInventedNativeExportJournal)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	ProjectedBonusGraph graph;
	battle::CUnitStateDetached unit(incoming, &graph.view(*incoming));
	ProjectedBonusRoster roster({&unit});
	graph.bind(*incoming, unit, roster);
	graph.installRecipientOverlays(*incoming, {});
	EXPECT_THROW(PreparedBonusGraphFinalization(graph, {}), std::logic_error);
}

TEST_F(LegacyRecipientMovementTest, NativeResolvedMultiplicityDoesNotBecomeRecipientStackingAmbiguity)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	releaseKnown();
	// The SAME native record is genuinely owned at two inherited occurrences.
	// Raw multiplicity is intentional; ordinary native stacking resolves it.
	auto record = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 0, BonusSourceID());
	incoming->addNewBonus(record);
	defenderSideHero->addNewBonus(record);
	const CSelector exact([record](const Bonus * bonus) { return bonus == record.get(); });
	ASSERT_EQ(incoming->getUnstackedBonuses(exact)->size(), 2);
	ASSERT_EQ(incoming->getAllBonuses(exact)->size(), 1);
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.originalAttackCount, 1);
	ASSERT_EQ(result.hits.size(), 2);
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
}

TEST_F(LegacyRecipientMovementTest, UnresolvedRecipientStackingCollisionStillRefusesBeforeInjury)
{
	ASSERT_NO_FATAL_FAILURE(prepareLegacy());
	releaseKnown();
	Bonus inherited(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 3, BonusSourceID());
	inherited.stacking = "test:legacy-recipient-collision";
	incoming->addNewBonus(std::make_shared<Bonus>(inherited));
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	Bonus local(inherited);
	local.val = 5;
	known->getForUpdate(incoming->unitId())->addUnitBonus({local});
	const CSelector selector([&inherited](const Bonus * bonus) { return bonus->stacking == inherited.stacking; });
	ASSERT_EQ(known->battleGetUnitByID(incoming->unitId())->getAllBonuses(selector)->size(), 2);
	const auto before = incoming->acquireState()->save();
	auto result = project();
	EXPECT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::UNSUPPORTED_REACTION_POLICY);
	EXPECT_FALSE(result.meleeSequenceComplete());
	EXPECT_TRUE(result.hits.empty());
	EXPECT_EQ(incoming->acquireState()->save(), before);
}
