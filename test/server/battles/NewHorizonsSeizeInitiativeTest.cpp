/*
 * NewHorizonsSeizeInitiativeTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "BattleTestFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/NewHorizonsSeizeInitiative.h"
#include "../../../lib/battle/NewHorizonsRapidResponse.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/ScopeGuard.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#include "../../../AI/BattleAI/HypotheticBattle.h"
#endif

namespace
{
class NewHorizonsSeizeInitiativeTest : public BattleTestFixture
{
protected:
	CStack * first = nullptr;
	CStack * second = nullptr;
	CStack * third = nullptr;
	CStack * enemy = nullptr;
	CStack * otherEnemy = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		const JsonNode combat(JsonPath::builtin("config/newHorizonsCombat"));
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, combat["combat"]["heroCommands"]);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}
	void selectCommandPerk(const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")),
			rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed, attackerSideHero->getPerkOfferWeightContext());
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == "new-horizons:command"
					&& offers[choice].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:command", perk));
					return;
				}
		}
		FAIL() << "No legal Command offer for " << perk;
	}
	void prepare(bool selected = true, bool rapid = false)
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(selected)
		{
			selectCommandPerk("new-horizons:command.aggressiveCommander", MasteryLevel::BASIC);
			selectCommandPerk("new-horizons:command.veteranCommander", MasteryLevel::ADVANCED);
			selectCommandPerk("new-horizons:command.seizeInitiative", MasteryLevel::EXPERT);
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:command", "new-horizons:command.seizeInitiative"));
		}
		if(rapid)
		{
			const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:battlecraft"));
			for(const auto & step : {std::pair<std::string, MasteryLevel::Type>{"new-horizons:battlecraft.entrench", MasteryLevel::BASIC},
				std::pair<std::string, MasteryLevel::Type>{std::string(newHorizonsRapidResponse::PERK_KEY), MasteryLevel::ADVANCED}})
			{
				attackerSideHero->setSecSkillLevel(skill, step.second, ChangeValueMode::ABSOLUTE);
				const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
				bool found = false;
				for(uint64_t seed = 0; seed < 4096 && !found; ++seed)
				{
					const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed, attackerSideHero->getPerkOfferWeightContext());
					for(size_t choice = 0; choice < offers.size(); ++choice)
						if(offers[choice].selection.perkId == step.first)
						{
							gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
							found = true;
							break;
						}
				}
				ASSERT_TRUE(found);
				ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:battlecraft", step.first));
			}
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		first = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(70), 10000);
		second = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(89), 10000);
		third = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(108), 10000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 10000);
		otherEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(75), 10000);
		SetStackEffect control;
		control.battleID = BattleID(0);
		for(const auto * unit : {first, second, third, enemy, otherEnemy})
			control.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{
				Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID())});
		gameHandler->sendAndApply(control);
		beginCombat();
		activate(first);
	}
	void activate(const CStack * unit, BattleUnitTurnReason reason = BattleUnitTurnReason::TURN_QUEUE)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0); active.stack = unit->unitId(); active.reason = reason;
		gameHandler->sendAndApply(active);
	}
	bool issue(HeroCommand command = HeroCommand::CHARGE)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}
	battle::Units queue(bool reordered = true)
	{
		std::vector<battle::Units> turns;
		battle()->battleGetTurnOrder(turns, 0, 1, 0, BattleSide::NONE, true, reordered);
		return turns.empty() ? battle::Units{} : turns.front();
	}
};
}

TEST_F(NewHorizonsSeizeInitiativeTest, GrandMarshalPaidOrderAndNormalCompletionNeverReorderExistingSlots)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = queue(false);
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	EXPECT_EQ(queue(true), before);
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER).remaining, resources.remaining - 1);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(first)));
	EXPECT_EQ(queue(true), queue(false));
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
}

TEST_F(NewHorizonsSeizeInitiativeTest, RejectedOrderCannotSpendCommandOrCreateAnActivationReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_FALSE(issue(HeroCommand::NONE));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), resources);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	EXPECT_EQ(queue(true), queue(false));
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
}

TEST_F(NewHorizonsSeizeInitiativeTest, ControlChangeCannotCreateADeferredGrandMarshalRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	SetStackEffect control;
	control.battleID = BattleID(0);
	control.toAdd.emplace_back(second->unitId(), std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_NO_THROW(gameHandler->sendAndApply(control));
	EXPECT_EQ(queue(true), queue(false));
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
}

TEST_F(NewHorizonsSeizeInitiativeTest, DescriptorKeepsGrandMarshalOwnershipAndDisabledQueueState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	const auto descriptor = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(descriptor, nullptr);
	EXPECT_EQ(descriptor->getCommandResources(BattleSide::ATTACKER), resources);
	EXPECT_EQ(descriptor->getSeizeInitiativeState(), SeizeInitiativeState{});
	EXPECT_TRUE(descriptor->getSideHero(BattleSide::ATTACKER)->hasActivePerk("new-horizons:command",
		"new-horizons:command.seizeInitiative"));
}

TEST_F(NewHorizonsSeizeInitiativeTest, ReservedLeafReceiptCodecCannotEnableRetiredReordering)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SeizeInitiativeState receipt;
	receipt.sides[0].enabled = true; receipt.round = 1;
	receipt.active = first->unitId(); receipt.activeNormal = true;
	receipt.normalCompleted = {second->unitId()};
	ASSERT_NO_THROW(receipt.validateShape());
	CMemorySerializer wire;
	ASSERT_NO_THROW(wire.oser & receipt);
	SeizeInitiativeState decoded;
	ASSERT_NO_THROW(wire.iser & decoded);
	EXPECT_EQ(decoded, receipt);
	const auto queueBefore = queue();
	EXPECT_THROW(battle()->setSeizeInitiativeState(decoded), std::runtime_error);
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
	EXPECT_EQ(queue(), queueBefore);
	CMemorySerializer old;
	old.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_SEIZE_INITIATIVE) - 1);
	EXPECT_THROW(old.oser & receipt, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST_F(NewHorizonsSeizeInitiativeTest, StaleCompletionPacketCannotChangeActionsResourcesOrQueue)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleNormalActivationCompleted packet;
	packet.battleID = BattleID(0); packet.unitId = first->unitId();
	packet.expected.sides[0].enabled = true; packet.expected.round = 1;
	packet.expected.active = first->unitId(); packet.expected.activeNormal = true;
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto queueBefore = queue();
	EXPECT_THROW(gameHandler->sendAndApply(packet), std::runtime_error);
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), resources);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(queue(), queueBefore);
}

TEST(NewHorizonsSeizeInitiativeProtocolTest, MalformedShapeIsRejectedAndAbsentLegacyStateRemainsWritable)
{
	SeizeInitiativeState state;
	state.sides[0].enabled = true; state.round = 1;
	state.normalCompleted = {7, 7};
	EXPECT_THROW(state.validateShape(), std::runtime_error);
	state.normalCompleted.clear(); state.activeNormal = true;
	EXPECT_THROW(state.validateShape(), std::runtime_error);
	state.activeNormal = false; state.sides[0].triggered = true;
	state.sides[0].awaitingAnchor = true;
	EXPECT_THROW(state.validateShape(), std::runtime_error);
	CMemorySerializer old;
	old.oser.version = old.iser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_SEIZE_INITIATIVE) - 1);
	SeizeInitiativeState empty, restored;
	EXPECT_NO_THROW(old.oser & empty);
	EXPECT_NO_THROW(old.iser & restored);
	EXPECT_EQ(empty, restored);
}

TEST_F(NewHorizonsSeizeInitiativeTest, RapidResponseKeepsItsIndependentWaiterWithoutASecondPerkInsertion)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	activate(second);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeWait(second)));
	ASSERT_TRUE(second->waited());
	activate(first);
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	BattleRapidResponseStateChanged response;
	response.battleID = BattleID(0); response.side = BattleSide::ATTACKER;
	response.expected = battle()->getRapidResponseState(response.side);
	response.state = newHorizonsRapidResponse::capture(*battle(), response.side);
	ASSERT_NO_THROW(gameHandler->sendAndApply(response));
	EXPECT_EQ(battle()->getRapidResponseState(response.side).pendingUnitId, second->unitId());
	std::vector<battle::Units> rapidOnly, full, one;
	battle()->battleGetTurnOrder(rapidOnly, 0, 1, 0, BattleSide::NONE, true, false);
	battle()->battleGetTurnOrder(full, 0, 1, 0, BattleSide::NONE, true, true);
	battle()->battleGetTurnOrder(one, 1, 1);
	EXPECT_EQ(full, rapidOnly);
	ASSERT_FALSE(full.empty()); ASSERT_FALSE(one.empty());
	ASSERT_EQ(one.front().size(), 1u);
	EXPECT_EQ(one.front().front(), full.front().front());
	for(const auto * unit : full.front()) EXPECT_EQ(std::count(full.front().begin(), full.front().end(), unit), 1);
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
}
