/*
 * NewHorizonsBattlePlanTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "BattleStartSnapshotFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <string_view>

namespace
{
constexpr auto commandSkill = "new-horizons:command";
constexpr auto battlePlanPerk = "new-horizons:command.battlePlan";
constexpr auto advancedCommandPerk = "new-horizons:command.veteranCommander";
constexpr auto doubleCommandPerk = "new-horizons:command.doubleCommand";

class NewHorizonsBattlePlanTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	bool offerContains(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(const auto & offer : offers)
				if(offer.selection.skillId == commandSkill && offer.selection.perkId == perkId)
					return true;
		}
		return false;
	}

	void acceptPerk(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == commandSkill && offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(commandSkill, std::string(perkId)));
					return;
				}
			}
		}
		FAIL() << "No legal perk offer for " << perkId;
	}

	void selectBattlePlan(CGHeroInstance * hero, bool alsoDoubleCommand = false)
	{
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		EXPECT_FALSE(offerContains(hero, battlePlanPerk))
			<< "Battle Plan requires Basic Command";
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, battlePlanPerk));
		acceptPerk(hero, battlePlanPerk);
		ASSERT_TRUE(hero->hasActivePerk(commandSkill, battlePlanPerk));

		if(alsoDoubleCommand)
		{
			hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			acceptPerk(hero, advancedCommandPerk);
			hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			acceptPerk(hero, doubleCommandPerk);
			ASSERT_TRUE(hero->hasActivePerk(commandSkill, doubleCommandPerk));
		}
	}

	bool issueOrder(BattleSide side, HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeHeroCommand(side, command));
	}

	bool issueTargetedOrder(BattleSide side, HeroCommand command, uint32_t targetId)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeTargetedHeroCommand(side, command, targetId));
	}

	static size_t ordinaryActivationCount(const RecordingGameServer & server)
	{
		return static_cast<size_t>(std::ranges::count_if(server.stackActivations,
			[](const BattleSetActiveStack & activation)
			{
				return activation.reason == BattleUnitTurnReason::TURN_QUEUE
					|| activation.reason == BattleUnitTurnReason::AUTOMATIC_ACTION;
			}));
	}
};
}

TEST_F(NewHorizonsBattlePlanTest, BasicPerkIsLegallyOfferedAndBattleStartDescriptorPreservesAvailability)
{
	startGame();
	selectBattlePlan(attackerSideHero);
	startBattle();

	const auto available = battle()->getPreCombatOrderState(BattleSide::ATTACKER);
	EXPECT_EQ(available.phase, PreCombatOrderState::Phase::AVAILABLE);
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::DEFENDER).phase,
		PreCombatOrderState::Phase::NOT_GRANTED);
	EXPECT_EQ(battle()->getActivationSerial(), 0);

	// The BattleStart payload is a descriptor snapshot, not a resumable live battle.
	const auto descriptor = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(descriptor, nullptr);
	EXPECT_EQ(descriptor->getPreCombatOrderState(BattleSide::ATTACKER), available);
	EXPECT_EQ(descriptor->getPreCombatOrderState(BattleSide::DEFENDER).phase,
		PreCombatOrderState::Phase::NOT_GRANTED);
	EXPECT_NO_THROW(descriptor->validatePreCombatOrderStructure());

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_MULTIPLE_ORDERS;
	EXPECT_THROW(older.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty())
		<< "An older writer must reject snapshotted Battle Plan availability before emitting bytes";
}

TEST_F(NewHorizonsBattlePlanTest, BothOpeningOrdersPrecedeCreatureActivationAndRetainHeroActions)
{
	startGame();
	selectBattlePlan(attackerSideHero);
	selectBattlePlan(defenderSideHero);
	startBattle();

	ASSERT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase,
		PreCombatOrderState::Phase::AVAILABLE);
	ASSERT_EQ(battle()->getPreCombatOrderState(BattleSide::DEFENDER).phase,
		PreCombatOrderState::Phase::AVAILABLE);
	const auto activationCountBefore = ordinaryActivationCount(server);

	// beginCombat submits the normal END_TACTIC_PHASE request and exercises the
	// authoritative onTacticsEnded flow. startBattle alone is setup-only here.
	beginCombat();

	const auto attackerOpening = battle()->getPreCombatOrderState(BattleSide::ATTACKER);
	ASSERT_EQ(attackerOpening.phase, PreCombatOrderState::Phase::ORDER_REQUIRED);
	EXPECT_EQ(attackerOpening.issuedRound, 1);
	EXPECT_EQ(battle()->getRound(), 1);
	EXPECT_EQ(battle()->getActivationSerial(), 0);
	EXPECT_TRUE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::DEFENDER));
	const auto * attackerAnchor = battle()->getStack(static_cast<int>(attackerOpening.anchorStackId), false);
	ASSERT_NE(attackerAnchor, nullptr);
	EXPECT_EQ(battle()->battleActiveUnit(), attackerAnchor);
	EXPECT_FALSE(attackerAnchor->movedThisRound);
	EXPECT_EQ(ordinaryActivationCount(server), activationCountBefore)
		<< "The temporary Order anchor must not count as a Creature Activation";

	const auto attackerLedger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto attackerCounts = attackerLedger.remainingCounts(1);
	EXPECT_EQ(attackerCounts.heroActions, 1u);
	EXPECT_EQ(attackerCounts.orderActions, 1u);
	const auto eligibleOpeningOrder = attackerLedger.eligibleAllowance(
		HeroActionAllowanceState::ActionKind::ORDER, 1);
	ASSERT_TRUE(eligibleOpeningOrder);
	EXPECT_EQ(eligibleOpeningOrder->source, HeroActionAllowanceState::GrantSource::BATTLE_PLAN);
	EXPECT_FALSE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND))
		<< "The temporary opening anchor has not taken a Creature Activation for Second Wind to extend";
	const auto startedBeforeSecondWind = server.startedActions.size();
	EXPECT_FALSE(issueTargetedOrder(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
		attackerAnchor->unitId()));
	EXPECT_EQ(server.startedActions.size(), startedBeforeSecondWind);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), attackerLedger);
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER), attackerOpening);
	EXPECT_TRUE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));

	const auto startedBeforeRejection = server.startedActions.size();
	const auto serialBeforeRejection = battle()->getActivationSerial();
	const auto stateBeforeRejection = battle()->getPreCombatOrderState(BattleSide::ATTACKER);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(BattleSide::ATTACKER), BattleAction::makeDefend(attackerAnchor)))
		<< "The anchor cannot take a Creature Action while the opening Order is mandatory";
	EXPECT_EQ(server.startedActions.size(), startedBeforeRejection);
	EXPECT_EQ(battle()->getActivationSerial(), serialBeforeRejection);
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER), stateBeforeRejection);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), attackerLedger);
	EXPECT_TRUE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));

	ASSERT_TRUE(issueOrder(BattleSide::ATTACKER, HeroCommand::BRACE));
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase,
		PreCombatOrderState::Phase::COMPLETED);
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->battleHasPendingPreCombatOrder(BattleSide::DEFENDER));
	EXPECT_EQ(battle()->getActivationSerial(), 0)
		<< "The second eligible side must receive its opening choice before any real activation";
	EXPECT_EQ(ordinaryActivationCount(server), activationCountBefore);
	const auto attackerAfterPlan = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_EQ(attackerAfterPlan.remainingCounts(1).heroActions, 1u)
		<< "BATTLE_PLAN must consume its own Order grant, not the ordinary Hero Action";
	EXPECT_EQ(attackerAfterPlan.countBattlePlanOrderGrants(1), 0u);

	const auto defenderOpening = battle()->getPreCombatOrderState(BattleSide::DEFENDER);
	ASSERT_EQ(defenderOpening.phase, PreCombatOrderState::Phase::ORDER_REQUIRED);
	const auto * defenderAnchor = battle()->getStack(static_cast<int>(defenderOpening.anchorStackId), false);
	ASSERT_NE(defenderAnchor, nullptr);
	EXPECT_EQ(battle()->battleActiveUnit(), defenderAnchor);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER).remainingCounts(1).heroActions, 1u);
	ASSERT_TRUE(issueOrder(BattleSide::DEFENDER, HeroCommand::BRACE));
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::DEFENDER).phase,
		PreCombatOrderState::Phase::COMPLETED);
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::DEFENDER));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER).remainingCounts(1).heroActions, 1u);
	EXPECT_GT(battle()->getActivationSerial(), 0);
	EXPECT_GT(ordinaryActivationCount(server), activationCountBefore)
		<< "Only after both opening choices resolve may the ordinary queue activate a creature";
}

TEST_F(NewHorizonsBattlePlanTest, NoLegalCurrentControllerAnchorCompletesWithoutStallingBattle)
{
	startGame();
	selectBattlePlan(attackerSideHero);
	startBattle();
	std::size_t controlledAway = 0;
	for(const auto * descriptor : battle()->battleGetAllStacks(false))
	{
		if(descriptor->unitSide() != BattleSide::ATTACKER)
			continue;
		auto * stack = battle()->getStack(descriptor->unitId(), false);
		ASSERT_NE(stack, nullptr);
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
		EXPECT_EQ(battle()->playerToSide(battle()->battleGetOwner(stack)), BattleSide::DEFENDER);
		++controlledAway;
	}
	ASSERT_GT(controlledAway, 0u);

	ASSERT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase,
		PreCombatOrderState::Phase::AVAILABLE);
	beginCombat();

	EXPECT_EQ(battle()->getRound(), 1);
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase,
		PreCombatOrderState::Phase::COMPLETED);
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.countBattlePlanOrderGrants(1), 0u);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr)
		<< "Losing every currently controlled anchor must exhaust Battle Plan, not stall the battle";
}

TEST_F(NewHorizonsBattlePlanTest, CompletionIsOncePerCombatAndDoesNotTriggerDoubleCommand)
{
	startGame();
	selectBattlePlan(attackerSideHero, true);
	startBattle();
	beginCombat();
	ASSERT_TRUE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));

	ASSERT_TRUE(issueOrder(BattleSide::ATTACKER, HeroCommand::BRACE));
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase,
		PreCombatOrderState::Phase::COMPLETED);
	EXPECT_FALSE(battle()->getDoubleCommandState(BattleSide::ATTACKER).used)
		<< "The dedicated BATTLE_PLAN allowance is not a Hero-paid Order and cannot trigger Double Command";
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(1).heroActions, 1u);

	endRound();
	EXPECT_EQ(battle()->getRound(), 2);
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase,
		PreCombatOrderState::Phase::COMPLETED);
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.countBattlePlanOrderGrants(1), 0u)
		<< "Battle Plan must not be re-granted or reopened after round one";
}
