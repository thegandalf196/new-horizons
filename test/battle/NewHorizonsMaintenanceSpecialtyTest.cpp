/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsMaintenanceFixture.h"
#include "../../lib/battle/NewHorizonsMaintenanceSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/Limiters.h"
#include <stdexcept>
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../server/battles/BattleMaintenanceRecovery.h"
#include "../../server/battles/BattleActionProcessor.h"
#include "../../server/battles/BattleActivationEffects.h"
#include <algorithm>
namespace
{
namespace maintenance = newHorizonsMaintenanceSpecialties;
class NewHorizonsMaintenanceSpecialtyTest : public NewHorizonsMaintenanceFixture,
	public ::testing::WithParamInterface<int>
{
protected:
	void prepare(const char * hero) { NewHorizonsMaintenanceFixture::prepare(hero, GetParam()); }
	int expected(int budget, int deficit) const { return GetParam() == 0 ? std::min(budget, deficit) : 0; }
	static void wound(battle::CUnitState & unit, int amount)
	{
		int64_t damage = amount; unit.damage(damage);
	}
	void expectWorldIdentity()
	{
		const auto policy = attackerSideHero->getPrimaryGrowthRules();
		const auto bonuses = attackerSideHero->getExportedBonusList().toJsonNode();
		attackerSideHero->initHero(*gameHandler->randomizer);
		EXPECT_EQ(attackerSideHero->getPrimaryGrowthRules(), policy);
		EXPECT_EQ(attackerSideHero->getExportedBonusList().toJsonNode(), bonuses);
		CGameState restored; restored.preInit(LIBRARY); restored.loadFromMemory(gameState()->saveToMemory());
		const auto * loaded = restored.getHero(attackerSideHero->id); ASSERT_NE(loaded, nullptr);
		EXPECT_EQ(loaded->getPrimaryGrowthRules(), policy);
		EXPECT_EQ(loaded->getExportedBonusList().toJsonNode(), bonuses);
		EXPECT_EQ(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded), GetParam() != 1);
	}
};
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, JosephineBothInvertedGolemIdsUseOnlySurvivingTopWounds)
{
	prepare("core:josephine"); ASSERT_NE(upgraded, nullptr);
	for(auto * unit : {actor, upgraded})
	{
		const auto count = unit->getCount();
		EXPECT_EQ(maintenance::woundHealing(*battle(), *unit, maintenance::Trigger::VOLUNTARY_DEFEND), 0);
		wound(*unit, unit->getMaxHealth() + 5);
		EXPECT_EQ(unit->getCount(), count - 1);
		EXPECT_EQ(maintenance::woundHealing(*battle(), *unit, maintenance::Trigger::VOLUNTARY_DEFEND), expected(10, 5));
		EXPECT_EQ(maintenance::woundHealing(*battle(), *unit, maintenance::Trigger::ACTIVATION_START), 0);
	}
	wound(*victim, 5);
	EXPECT_EQ(maintenance::woundHealing(*battle(), *victim, maintenance::Trigger::VOLUNTARY_DEFEND), 0);
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, JosephineActualVoluntaryDefendRepairsWithoutRestoringCasualties)
{
	prepare("core:josephine"); forceActorFirst();
	wound(*actor, actor->getMaxHealth() + 5);
	const auto count = actor->getCount();
	const auto before = actor->getAvailableHealth();
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(actor)));
	EXPECT_EQ(actor->getCount(), count);
	EXPECT_EQ(actor->getAvailableHealth(), before + expected(10, 5));
	EXPECT_TRUE(actor->defended());
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, JosephineActualForcedDefendDoesNotRepair)
{
	prepare("core:josephine"); forceActorFirst(); wound(*actor, 5);
	const auto before = actor->getAvailableHealth();
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	BattleActionProcessor actions(gameHandler->battles.get(), gameHandler.get());
	ASSERT_TRUE(actions.makeAutomaticBattleAction(*battle(), BattleAction::makeDefend(actor)));
	EXPECT_EQ(actor->getAvailableHealth(), before);
	EXPECT_TRUE(actor->defended());
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, RepairPublisherRejectsInvoluntaryDeadGhostAndStoppedRecipients)
{
	prepare("core:josephine"); wound(*actor, 5);
	const auto before = actor->getAvailableHealth();
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(maintenance::woundHealing(*battle(), *actor, maintenance::Trigger::VOLUNTARY_DEFEND), 0);
	actor->removeBonuses(Selector::type()(BonusType::TIME_STOP));
	battleMaintenanceRecovery::onDefend(*gameHandler, *battle(), *actor, false);
	EXPECT_EQ(actor->getAvailableHealth(), before);
	auto detached = actor->acquireState(); detached->makeGhost();
	EXPECT_EQ(maintenance::woundHealing(*battle(), *detached, maintenance::Trigger::VOLUNTARY_DEFEND), 0);
	wound(*actor, actor->getAvailableHealth());
	EXPECT_FALSE(actor->alive());
	battleMaintenanceRecovery::onDefend(*gameHandler, *battle(), *actor, true);
	EXPECT_FALSE(actor->alive());
	EXPECT_EQ(actor->getCount(), 0);
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, GerwulfRealStartupRepairsExistingWoundsOnceWithoutQueueProbeHealing)
{
	prepare("core:gerwulf"); forceActorFirst(); wound(*actor, 30);
	const auto before = actor->getAvailableHealth();
	const auto count = actor->getCount();
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	EXPECT_EQ(actor->getAvailableHealth(), before + expected(25, 30));
	EXPECT_EQ(actor->getCount(), count);
	wound(*actor, 5);
	const auto after = actor->getAvailableHealth();
	for(int i = 0; i < 3; ++i)
	{
		std::vector<battle::Units> queue; battle()->battleGetTurnOrder(queue, 1, 0, -1);
		EXPECT_EQ(actor->getAvailableHealth(), after);
	}
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, GerwulfOnlyGenuineStartsRepairAndDeferredWaitDoesNot)
{
	prepare("core:gerwulf"); wound(*actor, 30);
	for(const auto reason : {BattleUnitTurnReason::ACTION_REJECTED, BattleUnitTurnReason::MASTER_GATE_CONTINUATION,
		BattleUnitTurnReason::PURSUIT_CONTINUATION, BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION,
		BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION, BattleUnitTurnReason::HERO_SPELLCAST,
		BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::CRISIS_ORDER, BattleUnitTurnReason::CRISIS_RESUME})
		EXPECT_EQ(maintenance::activationHealing(*battle(), *actor, reason), 0);
	actor->waiting = true;
	EXPECT_EQ(maintenance::activationHealing(*battle(), *actor, BattleUnitTurnReason::TURN_QUEUE), 0);
	EXPECT_EQ(maintenance::activationHealing(*battle(), *actor, BattleUnitTurnReason::AUTOMATIC_ACTION), 0);
	for(const auto reason : {BattleUnitTurnReason::MORALE, BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION})
		EXPECT_EQ(maintenance::activationHealing(*battle(), *actor, reason), expected(25, 30));
	actor->waiting = false;
	EXPECT_EQ(maintenance::activationHealing(*battle(), *actor, BattleUnitTurnReason::TURN_QUEUE), expected(25, 30));
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, GerwulfActualMasterGunnerContinuationDoesNotRepairAgain)
{
	prepare("core:gerwulf"); forceActorFirst(); beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	wound(*actor, 5);
	const auto before = actor->getAvailableHealth();
	ASSERT_TRUE(battle()->battleCanShoot(actor, victim->getPosition()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	EXPECT_EQ(actor->getAvailableHealth(), before);
	EXPECT_EQ(maintenance::activationHealing(*battle(), *actor, BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION), 0);
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, KrellionDefendAllowanceDoesNotLatchAfterStanceEndsOrResetUsedCounters)
{
	prepare("core:krellion"); forceActorFirst();
	const int ordinary = actor->counterAttacks.total();
	EXPECT_EQ(maintenance::defendRetaliations(*actor), 0);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(actor)));
	EXPECT_EQ(actor->counterAttacks.total(), ordinary + expected(1, 1));
	actor->counterAttacks.use(1);
	EXPECT_EQ(actor->counterAttacks.available(), std::max(0, ordinary + expected(1, 1) - 1));
	SetStackEffect expiry; expiry.battleID = BattleID(0);
	std::vector<Bonus> stance;
	for(const auto & bonus : *actor->getBonusesOfType(BonusType::UNIT_DEFENDING)) stance.push_back(*bonus);
	expiry.toRemove.emplace_back(actor->unitId(), stance); gameHandler->sendAndApply(expiry);
	actor->defending = false; // Fixture mirrors the existing afterGetsTurn action-flag clear.
	EXPECT_EQ(actor->counterAttacks.total(), ordinary);
	EXPECT_EQ(actor->counterAttacks.available(), std::max(0, ordinary - 1));
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, KrellionBothOgreFormsRespectNoRetaliationAndUnlimitedEligibility)
{
	prepare("core:krellion"); ASSERT_NE(upgraded, nullptr);
	for(auto * unit : {actor, upgraded})
	{
		unit->defending = true;
		EXPECT_EQ(maintenance::defendRetaliations(*unit), expected(1, 1));
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NO_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
		EXPECT_EQ(unit->counterAttacks.total(), 0);
		EXPECT_FALSE(unit->counterAttacks.canUse());
		unit->removeBonuses(Selector::type()(BonusType::NO_RETALIATION));
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::UNLIMITED_RETALIATIONS, BonusSource::OTHER, 1, BonusSourceID()));
		EXPECT_FALSE(unit->counterAttacks.isLimited());
		EXPECT_TRUE(unit->counterAttacks.canUse());
	}
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, OrdinaryRetaliationBonusesKeepTheirExistingWithinRoundLatch)
{
	prepare("core:krellion");
	const int ordinary = actor->counterAttacks.total();
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_RETALIATION, BonusSource::OTHER, 2, BonusSourceID()));
	EXPECT_EQ(actor->counterAttacks.total(), ordinary + 2);
	actor->removeBonuses(Selector::type()(BonusType::ADDITIONAL_RETALIATION).And(Selector::sourceType()(BonusSource::OTHER)));
	EXPECT_EQ(actor->counterAttacks.total(), ordinary + 2);
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, KrellionReservedMarkerCannotMasqueradeAsAnotherRetaliationSource)
{
	prepare("core:krellion");
	const auto original = newHorizonsHeroes::ordinaryCreatureSpecialtyBonus(*attackerSideHero);
	if(GetParam() == 1) { EXPECT_FALSE(original); return; }
	ASSERT_TRUE(original); EXPECT_TRUE(maintenance::isDefendRetaliationBonus(original.get()));
	for(int mutation = 0; mutation < 6; ++mutation)
	{
		auto invalid = *original;
		if(mutation == 0) invalid.limiter = std::make_shared<CCreatureTypeLimiter>(*CreatureID(CreatureID::decode("core:ogre")).toCreature(), false);
		if(mutation == 1) invalid.limiter = std::make_shared<CCreatureTypeLimiter>(*CreatureID(CreatureID::decode("core:orc")).toCreature(), true);
		if(mutation == 2) invalid.effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;
		if(mutation == 3) invalid.val = 2;
		if(mutation == 4) invalid.appliedByEnemy = true;
		if(mutation == 5) invalid.subtype = BonusSubtypeID(PrimarySkill::ATTACK);
		EXPECT_THROW(maintenance::isDefendRetaliationBonus(&invalid), std::runtime_error);
	}
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, KrellionWorldRoundtripAndReinitRetainCapturedIdentity)
{
	prepare("core:krellion");
	expectWorldIdentity();
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, JosephineWorldRoundtripAndReinitRetainCapturedIdentity)
{
	prepare("core:josephine");
	expectWorldIdentity();
}
TEST_P(NewHorizonsMaintenanceSpecialtyTest, GerwulfWorldRoundtripAndReinitRetainCapturedIdentity)
{
	prepare("core:gerwulf");
	expectWorldIdentity();
}
INSTANTIATE_TEST_SUITE_P(CurrentHistoricalZero, NewHorizonsMaintenanceSpecialtyTest, ::testing::Values(0, 1, 2));
