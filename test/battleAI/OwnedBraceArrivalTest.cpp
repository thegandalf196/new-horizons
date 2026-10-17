/*
 * OwnedBraceArrivalTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OwnedExportMeleeFixture.h"
#include "../../AI/BattleAI/MovementBonusProvenance.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"

class OwnedBraceArrivalTest : public OwnedExportMeleeTest
{
protected:
	void own()
	{
		captureOwned();
		ASSERT_NO_FATAL_FAILURE(startOwned());
	}
	std::optional<heroOrderValue::PreparedBraceStage> arrive()
	{
		return heroOrderValue::prepareBraceStage(environment.get(), known, action());
	}
	void retireArrival(std::optional<heroOrderValue::PreparedBraceStage> & stage)
	{
		stage.reset();
		known.reset();
		seed.reset(); // Last canonical query owners retire before native mutation.
	}
	void attackNative()
	{
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
	}
};

TEST_F(OwnedBraceArrivalTest, NativeTerrainUsesCanonicalArrivalRatherThanALimiterWhitelist)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	EXPECT_FALSE(movementBonusProvenance::invariantSources(*incoming));
	ASSERT_NO_FATAL_FAILURE(own());
	const auto saved = incoming->acquireState()->save();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto stage = arrive();
	ASSERT_TRUE(stage);
	EXPECT_EQ(stage->entry.voluntaryMovementDistance, 3);
	EXPECT_EQ(stage->branch->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(92));
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(89));
	EXPECT_EQ(incoming->acquireState()->save(), saved);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	retireArrival(stage);
	ASSERT_NO_FATAL_FAILURE(attackNative());
	EXPECT_EQ(incoming->getPosition(), BattleHex(92));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_EQ(server.attacks.front().stackAttacking, braced->unitId());
}

TEST_F(OwnedBraceArrivalTest, LandingLimiterSuppressesTheActualNativeLoopBeforeBrace)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_ATTACK,
		BonusSource::OTHER, -1, BonusSourceID());
	bonus->limiter = std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(92)});
	incoming->addNewBonus(bonus);
	ASSERT_EQ(incoming->getTotalAttacks(false), 1);
	ASSERT_NO_FATAL_FAILURE(own());
	auto stage = arrive();
	EXPECT_FALSE(stage);
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(89));
	retireArrival(stage);
	ASSERT_NO_FATAL_FAILURE(attackNative());
	EXPECT_EQ(incoming->getPosition(), BattleHex(92));
	EXPECT_EQ(incoming->getTotalAttacks(false), 0);
	EXPECT_TRUE(server.attacks.empty());
}

TEST_F(OwnedBraceArrivalTest, OriginatingUnitOnlyAddRetainsItsHistoryAndNativeComparableArrival)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	ASSERT_NO_FATAL_FAILURE(own());
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::OTHER,
		4, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	addOrigin(*incoming, bonus);
	ASSERT_NO_FATAL_FAILURE(startOwned());
	const int before = incoming->getAttack(false);
	auto stage = arrive();
	ASSERT_TRUE(stage);
	const int predicted = stage->branch->battleGetUnitByID(incoming->unitId())->getAttack(false);
	EXPECT_EQ(predicted, before + 4);
	EXPECT_EQ(incoming->getAttack(false), before);
	retireArrival(stage);
	incoming->addNewBonus(std::make_shared<Bonus>(bonus));
	ASSERT_NO_FATAL_FAILURE(attackNative());
	EXPECT_EQ(incoming->getAttack(false), predicted);
}

TEST_F(OwnedBraceArrivalTest, ChangedHeroAttackGrantIsExplicitlyIncompleteNotANativeFallback)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	ASSERT_NO_FATAL_FAILURE(own());
	Bonus grant(BonusDuration::ONE_BATTLE, BonusType::HERO_GRANTS_ATTACKS, BonusSource::OTHER,
		1, BonusSourceID(), BonusSubtypeID(incoming->creatureId()));
	grant.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	addOrigin(*incoming, grant);
	ASSERT_NO_FATAL_FAILURE(startOwned());
	const auto projected = known->initialExportSubject()->world().projectedHeroBonuses(BattleSide::DEFENDER, true);
	ASSERT_EQ(projected->valOfBonuses(Selector::type()(BonusType::HERO_GRANTS_ATTACKS)), 1);
	ASSERT_EQ(defenderSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS), 0);
	EXPECT_FALSE(arrive());
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(89));
	EXPECT_EQ(incoming->getPosition(), BattleHex(89));
}

TEST_F(OwnedBraceArrivalTest, EntangleUnpropagationCannotKeepReadingTheNativeHeroMarker)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	const SpellID entangle(SpellID::decode("new-horizons:entangle"));
	ASSERT_TRUE(entangle.hasValue());
	auto marker = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::BIND_EFFECT,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(entangle));
	marker->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	incoming->addNewBonus(marker);
	ASSERT_TRUE(defenderSideHero->hasBonusOfType(BonusType::BIND_EFFECT));
	ASSERT_NO_FATAL_FAILURE(own());
	known->moveUnit(incoming->unitId(), BattleHex(90)); // Actual forced-move hook, not a scalar mirror.
	const auto projected = known->initialExportSubject()->world().projectedHeroBonuses(BattleSide::DEFENDER, true);
	EXPECT_FALSE(projected->getFirst(Selector::type()(BonusType::BIND_EFFECT)));
	EXPECT_TRUE(defenderSideHero->hasBonusOfType(BonusType::BIND_EFFECT));
	EXPECT_FALSE(arrive()); // Hero routing remains pending; no complete forecast is invented.
	EXPECT_EQ(incoming->getPosition(), BattleHex(89));
	EXPECT_TRUE(vstd::contains(incoming->getExportedBonusList(), marker));
}

TEST_F(OwnedBraceArrivalTest, MovementActivatedOverwatchCannotBecomeAnAcceptedBracePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:battlecraft"));
	ASSERT_GE(skill.getNum(), 0);
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk("new-horizons:battlecraft",
		"new-horizons:battlecraft.overwatch", attackerSideHero->getPerkSkillRank("new-horizons:battlecraft")));
	attackerSideHero->applyPerkSelection({"new-horizons:battlecraft", "new-horizons:battlecraft.overwatch"});
	ASSERT_TRUE(newHorizonsBattlecraft::hasOverwatch(attackerSideHero));
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(75), 10);
	forceMaximumDamage(shooter);
	shooter->battlecraftOverwatchReadyRound = battle()->battleGetRound();
	auto stopped = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NOT_ACTIVE,
		BonusSource::OTHER, 1, BonusSourceID());
	auto adjacent = std::make_shared<UnitAdjacentLimiter>();
	adjacent->targetUnit = incoming->creatureId();
	stopped->limiter = std::make_shared<NoneOfLimiter>(std::vector<TLimiterPtr>{adjacent});
	// Initially outside range; arrival enables readiness and a legal adjacent shot.
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LIMITED_SHOOTING_RANGE, BonusSource::OTHER, 1, BonusSourceID()));
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::FREE_SHOOTING, BonusSource::OTHER, 1, BonusSourceID()));
	shooter->addNewBonus(stopped);
	ASSERT_TRUE(shooter->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_FALSE(battle()->isEnemyUnitWithinSpecifiedRange(shooter->getPosition(), incoming, 1));
	ASSERT_FALSE(battle()->battleOverwatchReady(shooter));
	ASSERT_NO_FATAL_FAILURE(own());
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto control = std::make_shared<HypotheticBattle>(environment.get(), known);
	control->getForUpdate(incoming->unitId())->position = BattleHex(92);
	const auto * projectedShooter = control->battleGetUnitByID(shooter->unitId());
	const auto * projectedMover = control->battleGetUnitByID(incoming->unitId());
	ASSERT_FALSE(projectedShooter->hasBonusOfType(BonusType::NOT_ACTIVE));
	ASSERT_TRUE(control->battleOverwatchReady(projectedShooter));
	ASSERT_TRUE(control->battleCanOverwatch(projectedShooter, projectedMover, BattleHex(89), BattleHex(92)));
	ASSERT_TRUE(control->battleCanShoot(projectedShooter, BattleHex(92)));
	control.reset();
	auto stage = arrive();
	EXPECT_FALSE(stage);
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(89));
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	retireArrival(stage);
	ASSERT_NO_FATAL_FAILURE(attackNative());
	EXPECT_TRUE(std::ranges::any_of(server.attacks, [shooter](const BattleAttack & hit)
	{
		return hit.stackAttacking == shooter->unitId() && hit.shot();
	}));
}
