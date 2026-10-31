/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsMachineSpecialtyFixture.h"
#include "../../lib/battle/NewHorizonsMachineSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include <stdexcept>

class NewHorizonsMachineSpecialtyTest : public NewHorizonsMachineSpecialtyFixture
{
protected:
	void assertAcceptedFollowUp(int mode, int percent)
	{
		ASSERT_NO_FATAL_FAILURE(prepare("core:torosar", mode));
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.masterGunner"));
		forceActorFirst();
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		const auto shots = actor->acquireState()->shots.available();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
		EXPECT_EQ(actor->acquireState()->shots.available(), shots - 1);
		EXPECT_TRUE(battle()->battleHasPendingRangedFollowUp(actor));
		EXPECT_EQ(battle()->battleGetRangedFollowUpDamagePercent(actor), percent);
	}
};

TEST_F(NewHorizonsMachineSpecialtyTest, CapturedTorosarAcceptedShotGrantsEightyPercent)
{
	ASSERT_NO_FATAL_FAILURE(assertAcceptedFollowUp(0, 80));
}
TEST_F(NewHorizonsMachineSpecialtyTest, MissingTorosarKeysKeepLegacySixtyPercent)
{
	ASSERT_NO_FATAL_FAILURE(assertAcceptedFollowUp(1, 60));
}
TEST_F(NewHorizonsMachineSpecialtyTest, ZeroTorosarKeysKeepLegacySixtyPercent)
{
	ASSERT_NO_FATAL_FAILURE(assertAcceptedFollowUp(2, 60));
}
TEST_F(NewHorizonsMachineSpecialtyTest, TorosarWithoutLearnedMasterGunnerGetsNoFreeShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar", 0, false));
	ASSERT_FALSE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.masterGunner"));
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.precisionBombardment"));
	EXPECT_EQ(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(attackerSideHero, 60), 60);
	forceActorFirst();
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(actor));
}
TEST_F(NewHorizonsMachineSpecialtyTest, FollowUpBoundsDoNotCreateAnAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar"));
	EXPECT_EQ(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(attackerSideHero, 0), 0);
	EXPECT_EQ(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(attackerSideHero, 90), 100);
	EXPECT_EQ(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(nullptr, 60), 60);
	EXPECT_THROW(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(attackerSideHero, -1), std::runtime_error);
	EXPECT_THROW(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(attackerSideHero, 101), std::runtime_error);
}
TEST_F(NewHorizonsMachineSpecialtyTest, TorosarInitiativeBonusIsFlatAndFixedDamageIsUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar"));
	EXPECT_EQ(actor->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 2);
	EXPECT_EQ(upgraded->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 0);
	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	EXPECT_EQ(siege->ballistaDamage, 50 + 2 * siege->siegeRating);
}
TEST_F(NewHorizonsMachineSpecialtyTest, HeavyBatteryBoostsOnlyExistingSiegeDerivedTerms)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gurnisson"));
	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	const int rating = siege->siegeRating;
	EXPECT_EQ(siege->ballistaDamage, 50 + 2 * rating + (2 * rating) * 25 / 100);
	EXPECT_EQ(siege->catapultStructuralDamage, 100 + 3 * rating + (3 * rating) * 25 / 100);
	EXPECT_EQ(siege->firstAidHealing, 75 + 3 * rating);
	EXPECT_EQ(siege->defensiveTowerDamage, 60 + 3 * rating / 2);
}
TEST_F(NewHorizonsMachineSpecialtyTest, HeavyBatteryRoundsTheExistingTermBeforeItsBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gurnisson"));
	auto rules = attackerSideHero->getCapabilityRules();
	rules["siege"]["outputs"]["ballistaDamage"]["siegeCoefficientHalf"].Integer() = 3;
	EXPECT_EQ(newHorizonsMachineSpecialties::siegeOutput(*attackerSideHero, rules, 1, "ballistaDamage"), 51);
	EXPECT_EQ(newHorizonsMachineSpecialties::siegeOutput(*attackerSideHero, rules, 3, "ballistaDamage"), 55);
	EXPECT_EQ(newHorizonsMachineSpecialties::siegeOutput(*attackerSideHero, rules, 0, "ballistaDamage"), 50);
}
TEST_F(NewHorizonsMachineSpecialtyTest, HeavyBatteryDoesNotLeakIntoNonCatapultStructuralAdapters)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gurnisson"));
	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	const int ordinary = newHorizonsHeroes::capabilitySiegeOutput(attackerSideHero->getCapabilityRules(), siege->siegeRating, "catapultStructuralDamage");
	EXPECT_EQ(newHorizonsMachineSpecialties::catapultStructuralOutput(*attackerSideHero, *actor, siege->siegeRating, siege->catapultStructuralDamage), ordinary);
	EXPECT_EQ(battle()->battleGetCatapultStructuralDamage(actor, 0), 0);
}
TEST_F(NewHorizonsMachineSpecialtyTest, MissingHeavyBatteryKeepsOrdinaryMachineOutputs)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gurnisson", 1));
	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	EXPECT_EQ(siege->ballistaDamage, 50 + 2 * siege->siegeRating);
	EXPECT_EQ(siege->catapultStructuralDamage, 100 + 3 * siege->siegeRating);
}
TEST_F(NewHorizonsMachineSpecialtyTest, ZeroHeavyBatteryKeepsOrdinaryMachineOutputs)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gurnisson", 2));
	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	EXPECT_EQ(siege->ballistaDamage, 50 + 2 * siege->siegeRating);
	EXPECT_EQ(siege->catapultStructuralDamage, 100 + 3 * siege->siegeRating);
}
TEST_F(NewHorizonsMachineSpecialtyTest, HeavyBatteryRecognizesActualCatapultIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gurnisson"));
	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	CStackBasicDescriptor descriptor(CreatureID::CATAPULT, 1);
	CStack catapult(&descriptor, PlayerColor(0), 70, BattleSide::ATTACKER);
	EXPECT_EQ(newHorizonsMachineSpecialties::catapultStructuralOutput(*attackerSideHero, catapult, siege->siegeRating, siege->catapultStructuralDamage), siege->catapultStructuralDamage);
}
