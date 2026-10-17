/*
 * OwnedExportBranchQueryTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OwnedExportMeleeFixture.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"

TEST_F(OwnedExportMeleeTest, DirectScalarMovementUsesCurrentCanonicalPositionNotAStaleArenaMirror)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	captureOwned();
	auto record = aura(BonusDuration::ONE_BATTLE);
	record.limiter = std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(90)});
	addOrigin(*incoming, record);
	startOwned();
	auto left = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto right = std::make_shared<HypotheticBattle>(environment.get(), known);
	const int originalAttack = right->battleGetUnitByID(incoming->unitId())->getAttack(false);
	left->getForUpdate(incoming->unitId())->position = BattleHex(90);
	EXPECT_EQ(left->battleGetUnitByID(incoming->unitId())->getAttack(false), originalAttack + 4);
	EXPECT_EQ(right->battleGetUnitByID(incoming->unitId())->getAttack(false), originalAttack);
	EXPECT_EQ(incoming->getPosition(), BattleHex(89));
	// This is callback/query parity, not a claim that the movement-invariant
	// melee adapter can yet admit a position-sensitive arrival stage.
	EXPECT_FALSE(left->getForUpdate(incoming->unitId())->hasMovementInvariantBonusSources());
}

TEST_F(OwnedExportMeleeTest, ActualOriginatingPropagationUpdaterSeesTheCurrentSourceCountAtAdd)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	captureOwned();
	startOwned();
	auto source = known->getForUpdate(incoming->unitId());
	const int originalAttack = known->battleGetUnitByID(braced->unitId())->getAttack(false);
	int64_t damage = 10LL * source->getMaxHealth();
	source->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_EQ(source->getCount(), 90);
	auto record = aura(BonusDuration::ONE_BATTLE);
	record.propagationUpdater = std::make_shared<TimesStackSizeUpdater>(0, 1000, 10, 1);
	source->addUnitBonus({record});
	EXPECT_EQ(known->battleGetUnitByID(braced->unitId())->getAttack(false), originalAttack + 9);
	damage = 10LL * source->getMaxHealth();
	source->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_EQ(source->getCount(), 80);
	auto continuation = std::make_shared<HypotheticBattle>(environment.get(), known);
	source.reset();
	known.reset();
	seed.reset();
	EXPECT_EQ(continuation->battleGetUnitByID(braced->unitId())->getAttack(false), originalAttack + 9);
	EXPECT_EQ(continuation->battleGetUnitByID(incoming->unitId())->getCount(), 80);
}

TEST_F(OwnedExportMeleeTest, RealOwnedSequenceKeepsBothRngsAndOriginalParentStateUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	captureOwned();
	addOrigin(*incoming, aura(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE));
	startOwned();
	const auto parent = known->battleGetUnitByID(incoming->unitId())->acquireState()->save();
	const auto native = incoming->acquireState()->save();
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->acquireState()->save(), parent);
	EXPECT_EQ(incoming->acquireState()->save(), native);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_FALSE(result.activationComplete);
	EXPECT_FALSE(result.absoluteHorizonComplete);
}

TEST_F(OwnedExportMeleeTest, ForeignSiblingStateCannotMasqueradeAsThisBranchsCanonicalQueryOwner)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	captureOwned();
	startOwned();
	auto left = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto right = std::make_shared<HypotheticBattle>(environment.get(), known);
	const auto native = incoming->acquireState()->save();
	right->stackStates[incoming->unitId()] = left->getForUpdate(incoming->unitId());
	EXPECT_THROW(right->battleGetUnitByID(incoming->unitId()), std::invalid_argument);
	EXPECT_EQ(incoming->acquireState()->save(), native);
	EXPECT_EQ(left->battleGetUnitByID(incoming->unitId())->getAvailableHealth(), incoming->getAvailableHealth());
}

TEST_F(OwnedExportMeleeTest, CreatureAuraHasAnActualNativeRecipientButNeverChangesEitherHero)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	releaseKnown();
	const int incomingAttack = incoming->getAttack(false), bracedAttack = braced->getAttack(false);
	auto record = std::make_shared<Bonus>(aura(BonusDuration::ONE_BATTLE));
	incoming->addNewBonus(record);
	ASSERT_TRUE(vstd::contains(*incoming->getUnstackedBonuses(Selector::all), record));
	ASSERT_TRUE(vstd::contains(*braced->getUnstackedBonuses(Selector::all), record));
	ASSERT_FALSE(vstd::contains(*attackerSideHero->getUnstackedBonuses(Selector::all), record));
	ASSERT_FALSE(vstd::contains(*defenderSideHero->getUnstackedBonuses(Selector::all), record));
	ASSERT_EQ(incoming->getAttack(false), incomingAttack + 4);
	ASSERT_EQ(braced->getAttack(false), bracedAttack + 4);
	captureOwned();
	ASSERT_NO_FATAL_FAILURE(startOwned());
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getAttack(false), incomingAttack + 4);
	EXPECT_EQ(known->battleGetUnitByID(braced->unitId())->getAttack(false), bracedAttack + 4);
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete()) << static_cast<int>(result.stop);
	const auto frozen = freezeOracle(result);
	retire(result);
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
}

TEST_F(OwnedExportMeleeTest, AnActualProjectedHeroAuraStillCannotBypassTheFullIdentityWitness)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	captureOwned();
	auto record = aura(BonusDuration::ONE_BATTLE, BonusType::UNLIMITED_RETALIATIONS);
	record.limiter.reset(); // Explicit negative control outside this creature-only fixture domain.
	addOrigin(*incoming, record);
	ASSERT_NO_FATAL_FAILURE(startOwned());
	const auto projected = seed->world().projectedHeroBonuses(BattleSide::ATTACKER, true);
	ASSERT_FALSE(projected->empty());
	EXPECT_FALSE(attackerSideHero->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	EXPECT_TRUE(vstd::contains_if(*projected, [](const auto & bonus)
	{
		return bonus->type == BonusType::UNLIMITED_RETALIATIONS;
	}));
	const auto result = project();
	EXPECT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::INPUT_OR_MOVEMENT_UNAVAILABLE);
	EXPECT_FALSE(result.branch);
	EXPECT_TRUE(result.hits.empty());
	EXPECT_FALSE(attackerSideHero->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
}
