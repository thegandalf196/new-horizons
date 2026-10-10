/*
 * NewHorizonsRapidResponseTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/ScopeGuard.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/battle/NewHorizonsRapidResponse.h"
#include "../../../lib/battle/NewHorizonsHeroicSpirit.h"
#include "../../../lib/battle/NewHorizonsSeizeInitiative.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#endif

namespace
{
class NewHorizonsRapidResponseTest : public BattleTestFixture
{
protected:
	CStack * first = nullptr;
	CStack * second = nullptr;
	CStack * enemy = nullptr;
	CStack * otherEnemy = nullptr;
	bool jointMorale = false;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		if(jointMorale)
		{
			JsonNode chance;
			for(int index = 0; index < 10; ++index)
				chance.Vector().emplace_back(100);
			loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, chance);
			loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
			// Captured New Horizons Morale takes precedence over legacy chance settings.
			auto magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
			magic["morale"]["goodChance"] = chance;
			magic["morale"]["diceSize"].Integer() = 100;
			magic.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		}
	}
	void selectJointPerk(CGHeroInstance * hero, const std::string & skill,
		const std::string & perk, MasteryLevel::Type rank)
	{
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(lookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == skill && offers[choice].selection.perkId == perk)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "No legal joint offer for " << perk;
	}
	void prepare(bool selected = true, bool quartermaster = false, bool heroicJoint = false)
	{
		jointMorale = heroicJoint;
		startGame();
		const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:battlecraft"));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(selected)
		{
			selectJointPerk(attackerSideHero, "new-horizons:battlecraft",
				"new-horizons:battlecraft.entrench", MasteryLevel::BASIC);
			selectJointPerk(attackerSideHero, "new-horizons:battlecraft",
				std::string(newHorizonsRapidResponse::PERK_KEY), MasteryLevel::ADVANCED);
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:battlecraft",
				std::string(newHorizonsRapidResponse::PERK_KEY)));
		}
		if(heroicJoint)
		{
			selectJointPerk(attackerSideHero, "new-horizons:command", "new-horizons:command.aggressiveCommander", MasteryLevel::BASIC);
			selectJointPerk(attackerSideHero, "new-horizons:command", "new-horizons:command.veteranCommander", MasteryLevel::ADVANCED);
			selectJointPerk(attackerSideHero, "new-horizons:command", "new-horizons:command.seizeInitiative", MasteryLevel::EXPERT);
			selectJointPerk(defenderSideHero, newHorizonsHeroicSpirit::SKILL, "new-horizons:discipline.steadfast", MasteryLevel::BASIC);
			selectJointPerk(defenderSideHero, newHorizonsHeroicSpirit::SKILL, "new-horizons:discipline.holdFast", MasteryLevel::ADVANCED);
			selectJointPerk(defenderSideHero, newHorizonsHeroicSpirit::SKILL, newHorizonsHeroicSpirit::PERK, MasteryLevel::EXPERT);
		}
		if(quartermaster)
		{
			defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:warMachines")),
				MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({"new-horizons:warMachines",
				"new-horizons:warMachines.quartermaster"});
			ASSERT_TRUE(defenderSideHero->hasActivePerk("new-horizons:warMachines",
				"new-horizons:warMachines.quartermaster"));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		first = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(89), 10000);
		second = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(70), 10000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 10000);
		otherEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(75), 10000);
		SetStackEffect controls;
		controls.battleID = BattleID(0);
		for(const auto * unit : {first, second, enemy, otherEnemy})
			if(!heroicJoint || unit != enemy)
				controls.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{
					Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID())});
		if(heroicJoint)
			controls.toAdd.emplace_back(enemy->unitId(), std::vector<Bonus>{
				Bonus(BonusDuration::ONE_BATTLE, BonusType::MORALE, BonusSource::OTHER, 3, BonusSourceID())});
		gameHandler->sendAndApply(controls);
		beginCombat();
	}
	void activate(const CStack * unit)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = unit->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_EQ(battle()->battleActiveUnit(), unit);
	}
	void action(const CStack * unit, bool wait)
	{
		activate(unit);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(unit), wait ? BattleAction::makeWait(unit) : BattleAction::makeDefend(unit)));
	}
	void waitBoth()
	{
		action(first, true);
		action(second, true);
		ASSERT_TRUE(first->waited());
		ASSERT_TRUE(second->waited());
	}
	void capture()
	{
		BattleRapidResponseStateChanged update;
		update.battleID = BattleID(0);
		update.side = BattleSide::ATTACKER;
		update.expected = battle()->getRapidResponseState(update.side);
		update.state = newHorizonsRapidResponse::capture(*battle(), update.side);
		gameHandler->sendAndApply(update);
	}
};

#if ENABLE_BATTLE_AI
class RapidEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RapidEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}

TEST_F(NewHorizonsRapidResponseTest, RealWaitThenEnemyCompletionMovesLatestExistingDelayedSlotOnce)
{
	prepare();
	waitBoth();
	const auto * latest = newHorizonsRapidResponse::latestWaiter(*battle(), BattleSide::ATTACKER);
	ASSERT_NE(latest, nullptr);
	const auto latestId = latest->unitId();
	const auto serial = battle()->getActivationSerial();
	action(enemy, false);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), latestId);
	const auto state = battle()->getRapidResponseState(BattleSide::ATTACKER);
	EXPECT_EQ(state.lastUsedRound, battle()->getRound());
	EXPECT_FALSE(state.pending());
	EXPECT_EQ(battle()->getActivationSerial(), serial + 2); // enemy selection + one ordinary delayed activation
	const auto * moved = battle()->battleActiveUnit();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(moved)));
	const auto * remaining = first->unitId() == latestId ? second : first;
	ASSERT_TRUE(remaining->waited());
	action(otherEnemy, false);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, state.lastUsedRound);
	std::vector<battle::Units> queue;
	battle()->battleGetTurnOrder(queue, 0, 1);
	ASSERT_FALSE(queue.empty());
	EXPECT_EQ(std::count_if(queue.front().begin(), queue.front().end(),
		[latestId](const auto * unit) { return unit->unitId() == latestId; }), 0);
}

TEST_F(NewHorizonsRapidResponseTest, EnemyWaitIsNotACompletedBoundary)
{
	prepare();
	waitBoth();
	action(enemy, true);
	EXPECT_FALSE(battle()->getRapidResponseState(BattleSide::ATTACKER).pending());
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, -1);
}

TEST_F(NewHorizonsRapidResponseTest, NoWaiterAndInactivePerkDoNotSpendUse)
{
	prepare(false);
	waitBoth();
	action(enemy, false);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER), RapidResponseState{});
}

TEST_F(NewHorizonsRapidResponseTest, EmptyWaitingPoolRetainsOpportunity)
{
	prepare();
	action(enemy, false);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, -1);
	EXPECT_FALSE(battle()->getRapidResponseState(BattleSide::ATTACKER).pending());
}

TEST_F(NewHorizonsRapidResponseTest, EarnedQuartermasterActivationFinishesBeforeDelayedSlot)
{
	prepare(true, true);
	waitBoth();
	auto * ballista = addStack(BattleSide::DEFENDER, CreatureID::BALLISTA, BattleHex(97), 1);
	auto * cart = addStack(BattleSide::DEFENDER, CreatureID::AMMO_CART, BattleHex(98), 1);
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(cart, nullptr);
	ASSERT_TRUE(cart->alive());
	const auto * latest = newHorizonsRapidResponse::latestWaiter(*battle(), BattleSide::ATTACKER);
	ASSERT_NE(latest, nullptr);
	const auto latestId = latest->unitId();
	activate(ballista);
	ASSERT_TRUE(battle()->battleCanShoot(ballista, first->getPosition()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeShotAttack(ballista, first)));
	EXPECT_EQ(battle()->battleActiveUnit(), ballista);
	ASSERT_EQ(battle()->getReducedExtraActivationState(BattleSide::DEFENDER).activeUnitId, ballista->unitId());
	const auto pending = battle()->getRapidResponseState(BattleSide::ATTACKER);
	EXPECT_EQ(pending.pendingUnitId, latestId);
	EXPECT_EQ(pending.lastUsedRound, -1);
	std::vector<battle::Units> queue;
	battle()->battleGetTurnOrder(queue, 2, 1);
	ASSERT_FALSE(queue.empty());
	ASSERT_EQ(queue.front().size(), 2u);
	EXPECT_EQ(queue.front()[0], ballista);
	EXPECT_EQ(queue.front()[1]->unitId(), latestId);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeShotAttack(ballista, first)));
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), latestId);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, battle()->getRound());
}

TEST_F(NewHorizonsRapidResponseTest, InvalidDeferredRecipientDoesNotSpendOrManufactureAnotherWaiter)
{
	prepare();
	waitBoth();
	capture();
	const auto saved = battle()->getRapidResponseState(BattleSide::ATTACKER);
	ASSERT_TRUE(saved.pending());
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(saved.pendingUnitId, UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	// A consumed transition is rejected before mutation; an invalid clear retains use.
	BattleRapidResponseStateChanged consume;
	consume.battleID = BattleID(0);
	consume.side = BattleSide::ATTACKER;
	consume.expected = saved;
	consume.state = saved;
	consume.state.lastUsedRound = battle()->getRound();
	consume.state.clearPending();
	consume.transition = BattleRapidResponseStateChanged::Transition::CONSUME;
	EXPECT_THROW(consume.validateAgainst(*battle()), std::runtime_error);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER), saved);
	auto cleared = newHorizonsRapidResponse::resolve(*battle(), BattleSide::ATTACKER, false);
	EXPECT_EQ(cleared.lastUsedRound, -1);
	EXPECT_FALSE(cleared.pending());
	action(enemy, false);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, -1);
	EXPECT_FALSE(battle()->getRapidResponseState(BattleSide::ATTACKER).pending());
}

TEST_F(NewHorizonsRapidResponseTest, NewRoundRestoresOpportunityWithoutCreatingAnActivation)
{
	prepare();
	waitBoth();
	action(enemy, false);
	const auto previousRound = battle()->getRound();
	ASSERT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, previousRound);
	BattleNextRound next;
	next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	ASSERT_GT(battle()->getRound(), previousRound);
	EXPECT_FALSE(battle()->getRapidResponseState(BattleSide::ATTACKER).pending());
	waitBoth();
	action(enemy, false);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, battle()->getRound());
}

TEST_F(NewHorizonsRapidResponseTest, CurrentControllerAndExistingTieOrderingDetermineWaiter)
{
	prepare();
	waitBoth();
	const auto * before = newHorizonsRapidResponse::latestWaiter(*battle(), BattleSide::ATTACKER);
	ASSERT_NE(before, nullptr);
	auto controlled = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::HYPNOTIZED,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::HYPNOTIZE)));
	controlled->turnsRemain = 2;
	controlled->val = PlayerColor(1).getNum();
	// Use the same owner-changing bonus contract as ordinary Hypnotize.
	const_cast<CStack *>(dynamic_cast<const CStack *>(before))->addNewBonus(controlled);
	EXPECT_NE(battle()->battleGetOwner(before), PlayerColor(0));
	EXPECT_FALSE(newHorizonsRapidResponse::eligible(*battle(), BattleSide::ATTACKER, before));
	const auto * after = newHorizonsRapidResponse::latestWaiter(*battle(), BattleSide::ATTACKER);
	ASSERT_NE(after, nullptr);
	EXPECT_NE(after->unitId(), before->unitId());
}

TEST_F(NewHorizonsRapidResponseTest, StackAndBattleBinaryRoundTripsPreserveWaitQueueAndPendingReceipt)
{
	prepare();
	waitBoth();
	capture();
	const auto saved = battle()->getRapidResponseState(BattleSide::ATTACKER);
	auto stackCopy = CMemorySerializer::deepCopy(*first, gameState().get());
	ASSERT_NE(stackCopy, nullptr);
	EXPECT_TRUE(stackCopy->waiting);
	EXPECT_TRUE(stackCopy->waitedThisTurn);
	stackCopy->localInit(battle());
	EXPECT_TRUE(stackCopy->waiting);
	EXPECT_TRUE(stackCopy->waitedThisTurn);
	auto copy = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(copy, nullptr);
	// The public callback resolves army IDs to this fixture's actual armies.
	// Restore their battle binding after exercising the normal decoded localInit.
	auto restoreBinding = vstd::makeScopeGuard([&]
	{
		copy.reset();
		battle()->localInit();
	});
	EXPECT_EQ(copy->getRapidResponseState(BattleSide::ATTACKER), saved);
	copy->localInit();
	const auto * restored = newHorizonsRapidResponse::pendingWaiter(*copy, BattleSide::ATTACKER);
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->unitId(), saved.pendingUnitId);
	EXPECT_TRUE(restored->waited());
}

TEST_F(NewHorizonsRapidResponseTest, OldStackBattleAndPacketWritersRejectBeforePrefix)
{
	prepare();
	waitBoth();
	capture();
	const auto old = [](const auto & value)
	{
		CMemorySerializer wire;
		wire.oser.version = static_cast<ESerializationVersion>(
			static_cast<int>(ESerializationVersion::NEW_HORIZONS_RAPID_RESPONSE) - 1);
		EXPECT_THROW(wire.oser & value, std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty());
	};
	old(*first);
	old(*battle());
	BattleRapidResponseStateChanged update;
	update.battleID = BattleID(0);
	update.side = BattleSide::ATTACKER;
	update.expected = battle()->getRapidResponseState(update.side);
	update.state = update.expected;
	old(update);
	BattleStart start;
	start.battleID = BattleID(0);
	start.info = CMemorySerializer::deepCopy(*battle(), gameState().get());
	old(start);
}

TEST(NewHorizonsRapidResponseProtocolTest, ShapeAndPreviousFormatEmptyState)
{
	RapidResponseState malformed;
	malformed.enabled = true;
	malformed.pendingRound = 1;
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);
	malformed.pendingUnitId = 4;
	malformed.lastUsedRound = 1;
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_RAPID_RESPONSE) - 1);
	RapidResponseState empty, restored;
	ASSERT_NO_THROW(wire.oser & empty);
	ASSERT_NO_THROW(wire.iser & restored);
	EXPECT_EQ(restored, empty);
}

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsRapidResponseTest, DetachedQueueCaptureConsumptionAndCopyDoNotMutateLiveState)
{
	prepare();
	waitBoth();
	RapidEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto projected = std::make_shared<HypotheticBattle>(&environment, callback);
	projected->completeRapidResponseActivation(enemy->unitId());
	const auto receipt = projected->getRapidResponseState(BattleSide::ATTACKER);
	ASSERT_TRUE(receipt.pending());
	EXPECT_FALSE(battle()->getRapidResponseState(BattleSide::ATTACKER).pending());
	HypotheticBattle branch(&environment, projected);
	EXPECT_EQ(branch.getRapidResponseState(BattleSide::ATTACKER), receipt);
	std::vector<battle::Units> queue;
	branch.battleGetTurnOrder(queue, 0, 1, -1);
	ASSERT_FALSE(queue.empty());
	ASSERT_FALSE(queue.front().empty());
	EXPECT_EQ(queue.front().front()->unitId(), receipt.pendingUnitId);
	branch.nextTurn(receipt.pendingUnitId, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(branch.getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, branch.getRound());
	EXPECT_EQ(projected->getRapidResponseState(BattleSide::ATTACKER), receipt);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, -1);
}

TEST_F(NewHorizonsRapidResponseTest, ExtraAndContinuationForecastsRetainPendingDelayedSlot)
{
	prepare();
	waitBoth();
	capture();
	const auto receipt = battle()->getRapidResponseState(BattleSide::ATTACKER);
	RapidEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(&environment, callback);
	projected.nextTurn(enemy->unitId(), BattleUnitTurnReason::MORALE);
	EXPECT_EQ(projected.getRapidResponseState(BattleSide::ATTACKER), receipt);
	projected.nextTurn(enemy->unitId(), BattleUnitTurnReason::PURSUIT_CONTINUATION);
	EXPECT_EQ(projected.getRapidResponseState(BattleSide::ATTACKER), receipt);
	std::vector<battle::Units> queue;
	projected.battleGetTurnOrder(queue, 2, 1);
	ASSERT_FALSE(queue.empty());
	ASSERT_EQ(queue.front().size(), 2u);
	EXPECT_EQ(queue.front()[0]->unitId(), enemy->unitId());
	EXPECT_EQ(queue.front()[1]->unitId(), receipt.pendingUnitId);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER), receipt);
}

TEST_F(NewHorizonsRapidResponseTest, GenuineCompletedActorStoppedByEffectsStillTriggersButSyntheticSlotDoesNot)
{
	prepare();
	waitBoth();
	RapidEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle genuine(&environment, callback), synthetic(&environment, callback);
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	stopped.parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(BattleSide::ATTACKER));
	genuine.addUnitBonus(enemy->unitId(), {stopped});
	synthetic.addUnitBonus(enemy->unitId(), {stopped});
	ASSERT_TRUE(genuine.battleGetUnitByID(enemy->unitId())->isTimeStopped());
	genuine.completeRapidResponseActivation(enemy->unitId());
	synthetic.completeRapidResponseActivation(enemy->unitId(), true);
	EXPECT_TRUE(genuine.getRapidResponseState(BattleSide::ATTACKER).pending());
	EXPECT_FALSE(synthetic.getRapidResponseState(BattleSide::ATTACKER).pending());
	EXPECT_FALSE(battle()->getRapidResponseState(BattleSide::ATTACKER).pending());
}
#endif

TEST_F(NewHorizonsRapidResponseTest, JointEarnedHeroicMoralePrecedesRapidWithoutRearmingCompletedNormalSlot)
{
	prepare(true, false, true);
	waitBoth();
	const auto * latest = newHorizonsRapidResponse::latestWaiter(*battle(), BattleSide::ATTACKER);
	ASSERT_NE(latest, nullptr);
	const auto latestId = latest->unitId();
	activate(enemy);
	ASSERT_GT(battle()->battleGetMorale(enemy), 0);
	ASSERT_FALSE(enemy->hadMorale);
	ASSERT_EQ(enemy->counterAttacks.total(), 1);
	const auto movementEndpoint = [&]()
	{
		const auto available = battle()->battleGetAvailableHexes(enemy, true);
		for(const auto & hex : enemy->getSurroundingHexes())
			if(hex != enemy->getPosition() && std::find(available.begin(), available.end(), hex) != available.end())
				return hex;
		return BattleHex(BattleHex::INVALID);
	};
	const auto firstEndpoint = movementEndpoint();
	ASSERT_TRUE(firstEndpoint.isAvailable());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMove(enemy, firstEndpoint)));
	// Actual positive existing Morale roll: Heroic survives precisely its
	// granting extra, while the response stays queued rather than duplicated.
	ASSERT_EQ(battle()->battleActiveUnit(), enemy);
	ASSERT_TRUE(enemy->hadMorale);
	EXPECT_TRUE(enemy->heroicSpiritRetaliation);
	EXPECT_FALSE(enemy->heroicSpiritMoralePending);
	EXPECT_EQ(enemy->counterAttacks.total(), 2);
	const auto pending = battle()->getRapidResponseState(BattleSide::ATTACKER);
	ASSERT_TRUE(pending.pending());
	EXPECT_EQ(pending.pendingUnitId, latestId);
	EXPECT_EQ(pending.lastUsedRound, -1);
	ASSERT_TRUE(battle()->getSeizeInitiativeState().completed(enemy->unitId()));
	EXPECT_FALSE(battle()->getSeizeInitiativeState().activeNormal);
	std::vector<battle::Units> full, one;
	battle()->battleGetTurnOrder(full, 0, 1);
	battle()->battleGetTurnOrder(one, 1, 1);
	ASSERT_FALSE(full.empty());
	ASSERT_GE(full.front().size(), 2u);
	ASSERT_FALSE(one.empty());
	ASSERT_EQ(one.front().size(), 1u);
	EXPECT_EQ(full.front()[0], enemy);
	EXPECT_EQ(full.front()[1]->unitId(), latestId);
	EXPECT_EQ(one.front()[0], full.front()[0]);
	for(const auto * unit : full.front())
		EXPECT_EQ(std::count(full.front().begin(), full.front().end(), unit), 1);

	const auto extraEndpoint = movementEndpoint();
	ASSERT_TRUE(extraEndpoint.isAvailable());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMove(enemy, extraEndpoint)));
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), latestId);
	EXPECT_FALSE(battle()->getRapidResponseState(BattleSide::ATTACKER).pending());
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, battle()->getRound());
	EXPECT_TRUE(battle()->getSeizeInitiativeState().completed(enemy->unitId()));
	EXPECT_TRUE(enemy->heroicSpiritRetaliation);
	// Existing lifecycle-boundary control, not a fabricated earned grant:
	// a separate genuine extra start expires Heroic, never its granting extra.
	BattleSetActiveStack independent;
	independent.battleID = BattleID(0);
	independent.stack = enemy->unitId();
	independent.reason = BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION;
	gameHandler->sendAndApply(independent);
	EXPECT_FALSE(enemy->heroicSpiritRetaliation);
	EXPECT_FALSE(enemy->heroicSpiritMoralePending);
	EXPECT_EQ(enemy->counterAttacks.total(), 1);
	EXPECT_TRUE(battle()->getSeizeInitiativeState().completed(enemy->unitId()));
	EXPECT_FALSE(battle()->getSeizeInitiativeState().activeNormal);
	EXPECT_EQ(battle()->getRapidResponseState(BattleSide::ATTACKER).lastUsedRound, battle()->getRound());
}
