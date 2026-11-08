/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsMachineSpecialtyFixture.h"
#include "../../lib/battle/NewHorizonsMachineSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/battle/BattleHexArray.h"
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
		EXPECT_EQ(server.castsOf(SpellID(SpellID::FIREBALL)).size(), mode == 0 ? 1u : 0u);
	}
};

TEST_F(NewHorizonsMachineSpecialtyTest, FieryShotKeepsOrdinarySixtyPercentMasterGunnerAllowance)
{
	ASSERT_NO_FATAL_FAILURE(assertAcceptedFollowUp(0, 60));
}
TEST_F(NewHorizonsMachineSpecialtyTest, AuthoredFieryShotOmissionKeepsOrdinarySixtyPercent)
{
	ASSERT_NO_FATAL_FAILURE(assertAcceptedFollowUp(1, 60));
}
TEST_F(NewHorizonsMachineSpecialtyTest, ExplicitZeroFieryShotPolicyIsRejectedAtAdmission)
{
	EXPECT_THROW(prepare("core:torosar", 2), std::runtime_error);
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
	EXPECT_EQ(server.castsOf(SpellID(SpellID::FIREBALL)).size(), 1);
}
TEST_F(NewHorizonsMachineSpecialtyTest, AcceptedMasterGunnerFollowUpAlsoCastsFireballWithoutAnotherHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(assertAcceptedFollowUp(0, 60));
	const auto mana = attackerSideHero->getManaAvailable();
	const auto shots = actor->shots.available();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	EXPECT_EQ(actor->shots.available(), shots - 1);
	EXPECT_EQ(server.castsOf(SpellID(SpellID::FIREBALL)).size(), 2);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	for(const auto & cast : server.castsOf(SpellID(SpellID::FIREBALL)))
	{
		EXPECT_FALSE(cast.announcement.castByHero);
		EXPECT_FALSE(cast.announcement.activeCast);
	}
}
TEST_F(NewHorizonsMachineSpecialtyTest, ActualLethalRearHexShotCentersFireballOnTheAcceptedImpact)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar", 0, false));
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:centaur"), BattleHex(10, 7), 1);
	ASSERT_NE(primary, nullptr); ASSERT_EQ(primary->getHexes().size(), 2);
	const auto head = primary->getPosition();
	BattleHex impact;
	for(const auto hex : primary->getHexes()) if(hex != head) impact = hex;
	ASSERT_TRUE(impact.isValid());
	BattleHex impactOnly, headOnly;
	for(const auto hex : impact.getNeighbouringTiles())
		if(hex.isAvailable() && BattleHex::getDistance(head, hex) > 1 && !battle()->battleGetUnitByPos(hex, true))
			impactOnly = hex;
	for(const auto hex : head.getNeighbouringTiles())
		if(hex.isAvailable() && BattleHex::getDistance(impact, hex) > 1 && !battle()->battleGetUnitByPos(hex, true))
			headOnly = hex;
	ASSERT_TRUE(impactOnly.isValid()); ASSERT_TRUE(headOnly.isValid());
	auto * inside = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), impactOnly, 1000);
	auto * outside = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), headOnly, 1000);
	ASSERT_NE(inside, nullptr); ASSERT_NE(outside, nullptr);
	const auto insideHealth = inside->getAvailableHealth(), outsideHealth = outside->getAvailableHealth();
	const auto primaryId = primary->unitId(), insideId = inside->unitId(), outsideId = outside->unitId();
	forceActorFirst(); forceMaximumDamage(actor); beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	auto shot = BattleAction::makeShotAttack(actor, primary); shot.aimToHex(impact);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), shot));
	const auto casts = server.castsOf(SpellID(SpellID::FIREBALL)); ASSERT_EQ(casts.size(), 1);
	EXPECT_EQ(casts.front().announcement.tile, impact);
	const auto * current = gameState()->getBattle(BattleID(0)); ASSERT_NE(current, nullptr);
	const auto * defeated = current->battleGetStackByID(primaryId, false); ASSERT_NE(defeated, nullptr);
	EXPECT_FALSE(defeated->alive());
	const auto * hit = current->battleGetStackByID(insideId, false);
	const auto * untouched = current->battleGetStackByID(outsideId, false);
	ASSERT_NE(hit, nullptr); ASSERT_NE(untouched, nullptr);
	EXPECT_LT(hit->getAvailableHealth(), insideHealth);
	EXPECT_EQ(untouched->getAvailableHealth(), outsideHealth);
}
TEST_F(NewHorizonsMachineSpecialtyTest, FollowUpBoundsDoNotCreateAnAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar"));
	EXPECT_EQ(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(attackerSideHero, 0), 0);
	EXPECT_EQ(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(attackerSideHero, 90), 90);
	EXPECT_EQ(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(nullptr, 60), 60);
	EXPECT_THROW(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(attackerSideHero, -1), std::runtime_error);
	EXPECT_THROW(newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(attackerSideHero, 101), std::runtime_error);
}
TEST_F(NewHorizonsMachineSpecialtyTest, FieryShotDoesNotGrantRetiredInitiativeAndFixedPhysicalDamageIsUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar"));
	EXPECT_EQ(actor->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 0);
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
