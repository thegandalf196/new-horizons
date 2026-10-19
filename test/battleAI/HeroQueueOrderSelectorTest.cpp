/*
 * HeroQueueOrderSelectorTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsRapidResponse.h"
#include "../../lib/battle/NewHorizonsSeizeInitiative.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"

#include <algorithm>

namespace heroQueueTest
{
TEST_F(HeroQueueSelectorTest, PaidSeizeAnchorCompletionReordersExistingRecipientBeforeEnemyStop)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:command",
		"new-horizons:command.aggressiveCommander", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:command",
		"new-horizons:command.veteranCommander", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:command",
		"new-horizons:command.seizeInitiative", MasteryLevel::EXPERT));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:divineMandate",
		"new-horizons:divineMandate.consecratedCasting", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(injure(actor, actor->getAvailableHealth() - 100));
	ASSERT_EQ(actor->getSurvivingMissingHealth(), 100);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto & receipt = battle()->getSeizeInitiativeState().sides.at(0);
	ASSERT_TRUE(receipt.enabled);
	ASSERT_TRUE(receipt.awaitingAnchor);
	ASSERT_EQ(receipt.anchor, active->unitId());
	ASSERT_NE(receipt.recipient, SeizeInitiativeState::NO_UNIT);
	const auto allowance = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::CURE);
	ASSERT_TRUE(allowance);
	ASSERT_EQ(allowance->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	spells::BattleCast cure(battle(), attackerSideHero, spells::Mode::HERO, spell);
	spells::Target target;
	target.emplace_back(actor);
	ASSERT_TRUE(spell->canBeCast(battle(), spells::Mode::HERO, attackerSideHero));
	ASSERT_TRUE(spell->battleMechanics(&cure)->canBeCastAt(target));
	RecordProperty("seizeRecipient", std::to_string(receipt.recipient));
	RecordProperty("seizeAnchor", std::to_string(receipt.anchor));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_TRUE(battle()->getSeizeInitiativeState().sides.at(0).awaitingAnchor);
}

TEST_F(HeroQueueSelectorTest, RapidResponseForecastMovesLatestExistingEqualInitiativeWaiterOnly)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:battlecraft",
		"new-horizons:battlecraft.entrench", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:battlecraft",
		std::string(newHorizonsRapidResponse::PERK_KEY), MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	// The same base initiative leaves the existing wait queue tie policy intact.
	effect(waiter, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_FLAT,
		BonusSource::OTHER, actor->getInitiative(0) - waiter->getInitiative(0), BonusSourceID()));
	ASSERT_EQ(actor->getInitiative(0), waiter->getInitiative(0));
	for(const auto * unit : {actor, waiter})
	{
		ASSERT_NO_FATAL_FAILURE(activate(unit));
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeWait(unit)));
		ASSERT_TRUE(unit->waited());
	}
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_NO_FATAL_FAILURE(spendEnemyAllowance());
	ASSERT_TRUE(battle()->getRapidResponseState(BattleSide::ATTACKER).enabled);
	ASSERT_FALSE(battle()->getRapidResponseState(BattleSide::ATTACKER).pending());
	const auto * latest = newHorizonsRapidResponse::latestWaiter(*battle(), BattleSide::ATTACKER);
	ASSERT_NE(latest, nullptr);
	ASSERT_TRUE(latest == actor || latest == waiter);
	const auto turns = queue();
	ASSERT_FALSE(turns.empty());
	const auto enemySlot = std::ranges::find(turns.front(), enemy->unitId());
	const auto waiterSlot = std::ranges::find(turns.front(), latest->unitId());
	ASSERT_NE(enemySlot, turns.front().end());
	ASSERT_NE(waiterSlot, turns.front().end());
	ASSERT_LT(enemySlot, waiterSlot);
	RecordProperty("latestWaiter", std::to_string(latest->unitId()));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_FALSE(battle()->getRapidResponseState(BattleSide::ATTACKER).pending());
	EXPECT_TRUE(actor->waited());
	EXPECT_TRUE(waiter->waited());
}

TEST_F(HeroQueueSelectorTest, CarriedDivineDisciplineRecipientCompletesOnlyInSelectorForecast)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:divineMandate",
		"new-horizons:divineMandate.consecratedCasting", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:divineMandate",
		"new-horizons:divineMandate.divineDiscipline", MasteryLevel::ADVANCED));
	attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, actor));
	const auto allowance = battle()->battleGetOrderActionAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(allowance);
	ASSERT_EQ(allowance->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	advanceRound();
	ASSERT_NO_FATAL_FAILURE(activate(active));
	const auto carried = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE);
	ASSERT_TRUE(carried);
	ASSERT_TRUE(carried->scheduledFor(actor->unitId(), battle()->getRound()));
	ASSERT_FALSE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);
	ASSERT_FALSE(battle()->battleCanUseMetamagicFollowup(BattleSide::ATTACKER));
	ASSERT_GT(battle()->calculateDmgRange(BattleAttackInfo(actor, enemy, 0, false)).damage.min,
		enemy->getAvailableHealth());
	ASSERT_NO_FATAL_FAILURE(expectBeforeEnemy(actor));
	ASSERT_NO_FATAL_FAILURE(characterize(false));
	EXPECT_TRUE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE)
		->scheduledFor(actor->unitId(), battle()->getRound()));
}

TEST_F(HeroQueueSelectorTest, EarnedSwiftSpawnHasExistingStablePrioritySlotBeforeCounterspellStop)
{
	ASSERT_NO_FATAL_FAILURE(world());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:elementalRebirth",
		"new-horizons:elementalRebirth.primalBurst", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, "new-horizons:elementalRebirth",
		"new-horizons:elementalRebirth.swiftRebirth", MasteryLevel::BASIC));
	attackerSideHero->clearSlots();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:archangel"), 100));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 101));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:peasant"), 101));
	ASSERT_NO_FATAL_FAILURE(startBattle());
	CStack * source = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
	{
		if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
			active = battle()->getStack(unit->unitId(), false);
		if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0))
			source = battle()->getStack(unit->unitId(), false);
		if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(1))
			enemy = battle()->getStack(unit->unitId(), false);
	}
	ASSERT_NE(active, nullptr);
	ASSERT_NE(source, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(newHorizonsElementalRebirth::isEligibleSource(*source));
	ASSERT_NO_FATAL_FAILURE(spendEnemyAllowance());
	const auto nextId = battle()->battleNextUnitId();
	StacksInjured packet;
	packet.battleID = BattleID(0);
	auto & hit = packet.stacks.emplace_back();
	hit.attackerID = active->unitId();
	hit.stackAttacked = source->unitId();
	hit.damageAmount = source->getAvailableHealth();
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), source->acquireState());
	gameHandler->sendAndApply(packet);
	actor = battle()->getStack(nextId, false);
	ASSERT_NE(actor, nullptr);
	ASSERT_TRUE(newHorizonsSwiftRebirth::priorityEligible(*actor, battle()->getRound()));
	// The current actor must not kill a queued Swift recipient before its slot.
	effect(active, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -static_cast<int64_t>(active->getMovementRange()), BonusSourceID()));
	ASSERT_EQ(active->getMovementRange(), 0u);
	ASSERT_GT(BattleHex::getDistance(active->getPosition(), actor->getPosition()), 2);
	// Give the known Cure a useful target without changing the earned Swift slot.
	ASSERT_EQ(active->getCount(), 100);
	ASSERT_NO_FATAL_FAILURE(injure(active, active->getAvailableHealth() - 100));
	ASSERT_EQ(active->getCount(), 100);
	ASSERT_EQ(active->getSurvivingMissingHealth(), 100);
	ASSERT_NO_FATAL_FAILURE(expectBeforeEnemy(actor));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_TRUE(newHorizonsSwiftRebirth::priorityEligible(*actor, battle()->getRound()));
}
}
