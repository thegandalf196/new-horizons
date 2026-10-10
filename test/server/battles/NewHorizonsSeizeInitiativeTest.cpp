/*
 * NewHorizonsSeizeInitiativeTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "BattleTestFixture.h"
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
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		// Only the pre-Seize populated-reader control captures the historical Crisis profile.
		const auto * info = ::testing::UnitTest::GetInstance()->current_test_info();
		if(info && std::string(info->name()) == "CurrentAndLegacyReadersReplacePopulatedStaleReceiptsBeforeAdmission")
		{
			for(auto & perk : perks["skills"]["new-horizons:command"]["perks"].Vector())
				if(perk["id"].String() == "new-horizons:command.crisisCommand")
					perk["effect"]["status"].String() = "planned";
			perks.setOverrideFlag(true);
			auto heroes = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
			heroes["startingSkills"].Struct().erase("startingBookReplacements");
			heroes.Struct().erase("lighthouseDeparture");
			heroes["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
			heroes["skillSpecialties"].Struct().erase("navigationStartReplacements");
			heroes.Struct().erase("defaultCreatureLineReplacements");
			heroes.Struct().erase("remainingSpellSpecialtyReplacements");
			auto & supported = heroes["nonDamageSpellSpecialties"]["spells"].Vector();
			supported.erase(std::remove_if(supported.begin(), supported.end(), [](const JsonNode & spell)
			{
				return spell.String() == "new-horizons:phantomArmy";
			}), supported.end());
			heroes.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroes);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		if(info && std::string(info->name()) == "CurrentAndLegacyReadersReplacePopulatedStaleReceiptsBeforeAdmission")
			isolateHistoricalAdventurePolicies(*loaded);
	}
	void selectCommandPerk(const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")),
			rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed);
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
					const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed);
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
	void complete(const CStack * unit)
	{
		BattleNormalActivationCompleted completion;
		completion.battleID = BattleID(0); completion.unitId = unit->unitId();
		completion.expected = battle()->getSeizeInitiativeState();
		gameHandler->sendAndApply(completion);
	}
	battle::Units queue(bool reordered = true)
	{
		std::vector<battle::Units> turns;
		battle()->battleGetTurnOrder(turns, 0, 1, 0, BattleSide::NONE, true, reordered);
		return turns.empty() ? battle::Units{} : turns.front();
	}
};
#if ENABLE_BATTLE_AI
class SeizeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SeizeEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}

TEST_F(NewHorizonsSeizeInitiativeTest, AcceptedPaidOrderCapturesLatestExceptActiveAndRealCompletionMovesExistingSlot)
{
	prepare();
	const auto original = queue(false);
	const CStack * expected = nullptr;
	for(auto it = original.rbegin(); it != original.rend(); ++it)
		if(*it != first && newHorizonsSeizeInitiative::eligible(*battle(), BattleSide::ATTACKER, *it))
		{ expected = dynamic_cast<const CStack *>(*it); break; }
	ASSERT_NE(expected, nullptr);
	ASSERT_TRUE(issue());
	const auto receipt = battle()->getSeizeInitiativeState().sides[0];
	EXPECT_TRUE(receipt.triggered);
	EXPECT_TRUE(receipt.awaitingAnchor);
	EXPECT_EQ(receipt.recipient, expected->unitId());
	EXPECT_EQ(queue().front(), first);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(first)));
	EXPECT_TRUE(battle()->getSeizeInitiativeState().completed(first->unitId()));
	EXPECT_FALSE(battle()->getSeizeInitiativeState().sides[0].awaitingAnchor);
	const auto next = queue();
	const auto friendly = std::find_if(next.begin(), next.end(), [&](const auto * unit)
		{ return battle()->battleGetOwner(unit) == PlayerColor(0); });
	ASSERT_NE(friendly, next.end());
	EXPECT_EQ((*friendly)->unitId(), expected->unitId());
	EXPECT_EQ(std::count(next.begin(), next.end(), expected), 1);
}

TEST_F(NewHorizonsSeizeInitiativeTest, EnemyPositionsAndRelativeFriendlyOrderArePreserved)
{
	prepare(); ASSERT_TRUE(issue()); complete(first);
	const auto selected = battle()->getSeizeInitiativeState().sides[0].recipient;
	const auto * recipient = battle()->battleGetUnitByID(selected);
	const auto * other = recipient == second ? third : second;
	battle::Units slots{first, enemy, other, otherEnemy, recipient};
	newHorizonsSeizeInitiative::reorder(*battle(), slots, true);
	EXPECT_EQ(slots, (battle::Units{first, enemy, recipient, otherEnemy, other}));
}

TEST_F(NewHorizonsSeizeInitiativeTest, RejectedOrderAndInactivePerkDoNotConsume)
{
	prepare(false);
	EXPECT_FALSE(issue(HeroCommand::NONE));
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
	ASSERT_TRUE(issue());
	EXPECT_EQ(battle()->getSeizeInitiativeState(), SeizeInitiativeState{});
}

TEST_F(NewHorizonsSeizeInitiativeTest, EmptyEligiblePoolConsumesFirstPaidTriggerAndCannotRetargetLater)
{
	prepare();
	auto ledger = battle()->getSeizeInitiativeState();
	ledger.normalCompleted = {second->unitId(), third->unitId()};
	std::sort(ledger.normalCompleted.begin(), ledger.normalCompleted.end());
	battle()->setSeizeInitiativeState(ledger);
	ASSERT_TRUE(issue());
	EXPECT_TRUE(battle()->getSeizeInitiativeState().sides[0].triggered);
	EXPECT_EQ(battle()->getSeizeInitiativeState().sides[0].recipient, SeizeInitiativeState::NO_UNIT);
	BattleNextRound next; next.battleID = BattleID(0); gameHandler->sendAndApply(next);
	activate(first);
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	EXPECT_EQ(battle()->getSeizeInitiativeState().sides[0].recipient, SeizeInitiativeState::NO_UNIT);
}

TEST_F(NewHorizonsSeizeInitiativeTest, FreeDedicatedOrderDoesNotTrigger)
{
	prepare();
	auto & allowances = battle()->getSide(BattleSide::ATTACKER).heroActionAllowances;
	allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, battle()->getRound());
	ASSERT_EQ(allowances.eligibleAllowance(HeroActionAllowanceState::ActionKind::ORDER,
		battle()->getRound())->allowance, HeroActionAllowanceState::AllowanceKind::ORDER);
	ASSERT_TRUE(issue());
	EXPECT_FALSE(battle()->getSeizeInitiativeState().sides[0].triggered);
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::BRACE));
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	EXPECT_TRUE(battle()->getSeizeInitiativeState().sides[0].triggered);
}

TEST_F(NewHorizonsSeizeInitiativeTest, WaitAndContinuationDoNotReleaseAnchorOrCompleteNormalSlot)
{
	prepare(); ASSERT_TRUE(issue());
	activate(first, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_TRUE(battle()->getSeizeInitiativeState().activeNormal);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeWait(first)));
	EXPECT_FALSE(battle()->getSeizeInitiativeState().completed(first->unitId()));
	EXPECT_TRUE(battle()->getSeizeInitiativeState().sides[0].awaitingAnchor);
	activate(first);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(first)));
	EXPECT_TRUE(battle()->getSeizeInitiativeState().completed(first->unitId()));
}

TEST_F(NewHorizonsSeizeInitiativeTest, ImmediateExtraStaysFirstAndDoesNotRestoreNormalEligibility)
{
	prepare(); ASSERT_TRUE(issue()); complete(first);
	activate(first, BattleUnitTurnReason::MORALE);
	ASSERT_FALSE(first->moved());
	EXPECT_TRUE(battle()->getSeizeInitiativeState().completed(first->unitId()));
	EXPECT_FALSE(newHorizonsSeizeInitiative::eligible(*battle(), BattleSide::ATTACKER, first));
	EXPECT_EQ(queue().front(), first);
	complete(first);
	EXPECT_TRUE(battle()->getSeizeInitiativeState().completed(first->unitId()));
}

TEST_F(NewHorizonsSeizeInitiativeTest, DeferredIncapacitatedRecipientIsNotReplaced)
{
	prepare(); ASSERT_TRUE(issue());
	const auto id = battle()->getSeizeInitiativeState().sides[0].recipient;
	SetStackEffect stopped;
	stopped.battleID = BattleID(0);
	stopped.toAdd.emplace_back(id, std::vector<Bonus>{
		Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(stopped);
	complete(first);
	EXPECT_FALSE(newHorizonsSeizeInitiative::eligible(*battle(), BattleSide::ATTACKER, battle()->battleGetUnitByID(id)));
	EXPECT_EQ(battle()->getSeizeInitiativeState().sides[0].recipient, id);
	EXPECT_TRUE(battle()->getSeizeInitiativeState().sides[0].triggered);
}

TEST_F(NewHorizonsSeizeInitiativeTest, CurrentControllerChangeInvalidatesChosenWithoutRetarget)
{
	prepare(); ASSERT_TRUE(issue());
	const auto id = battle()->getSeizeInitiativeState().sides[0].recipient;
	auto controlled = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::HYPNOTIZED,
		BonusSource::SPELL_EFFECT, PlayerColor(1).getNum(), BonusSourceID(SpellID(SpellID::HYPNOTIZE)));
	controlled->turnsRemain = 2;
	const_cast<CStack *>(dynamic_cast<const CStack *>(battle()->battleGetUnitByID(id)))->addNewBonus(controlled);
	EXPECT_EQ(battle()->battleGetOwner(battle()->battleGetUnitByID(id)), PlayerColor(1));
	complete(first);
	EXPECT_FALSE(newHorizonsSeizeInitiative::eligible(*battle(), BattleSide::ATTACKER, battle()->battleGetUnitByID(id)));
	EXPECT_EQ(battle()->getSeizeInitiativeState().sides[0].recipient, id);
}

TEST_F(NewHorizonsSeizeInitiativeTest, ReceiptAndNormalMarkersRoundTripAndOldWriterRejectsBeforePrefix)
{
	prepare(); ASSERT_TRUE(issue()); complete(first);
	const auto expected = battle()->getSeizeInitiativeState();
	CMemorySerializer current;
	auto saved = expected; current.oser & saved;
	SeizeInitiativeState loaded; current.iser & loaded;
	EXPECT_EQ(loaded, expected);
	CMemorySerializer old;
	old.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_SEIZE_INITIATIVE) - 1);
	EXPECT_THROW(old.oser & saved, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	auto copied = CMemorySerializer::deepCopy(*battle(), gameState().get());
	auto restore = vstd::makeScopeGuard([&] { copied.reset(); battle()->localInit(); });
	EXPECT_EQ(copied->getSeizeInitiativeState(), expected);
	copied->localInit();
	EXPECT_EQ(copied->getSeizeInitiativeState(), expected);
	BattleStart start; start.battleID = BattleID(0); start.info = std::move(copied);
	CMemorySerializer oldStart;
	oldStart.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_SEIZE_INITIATIVE) - 1);
	EXPECT_THROW(oldStart.oser & start, std::runtime_error);
	EXPECT_TRUE(oldStart.extractBuffer().empty());
}

TEST_F(NewHorizonsSeizeInitiativeTest, CurrentAndLegacyReadersReplacePopulatedStaleReceiptsBeforeAdmission)
{
	prepare(); ASSERT_TRUE(issue()); complete(first);
	const auto expected = battle()->getSeizeInitiativeState();
	ASSERT_TRUE(expected.enabled());
	std::vector<uint32_t> expectedDescriptorIds;
	for(const auto & unit : battle()->stacks)
		expectedDescriptorIds.push_back(unit->unitId());
	// REMOVE keeps ghost descriptors. The existing binary stack format carries
	// descriptors and queue flags, not the complete live health/ghost state.
	ASSERT_GT(expectedDescriptorIds.size(), battle()->battleGetAllUnits(false).size());
	for(const bool legacy : {false, true})
	{
		auto source = CMemorySerializer::deepCopy(*battle(), gameState().get());
		auto receiver = CMemorySerializer::deepCopy(*battle(), gameState().get());
		auto restore = vstd::makeScopeGuard([&]
		{
			receiver.reset();
			source.reset();
			battle()->localInit();
		});
		if(legacy)
			source->setSeizeInitiativeState(SeizeInitiativeState{});
		// Existing receipt references are deliberately stale. They describe the
		// receiver being replaced, not the incoming source, and must not be
		// admitted before decoding. Current incoming references remain validated.
		receiver->stacks.clear();
		ASSERT_EQ(receiver->getSeizeInitiativeState(), expected);
		CMemorySerializer wire;
		wire.iser.cb = gameState().get();
		if(legacy)
			wire.oser.version = wire.iser.version = static_cast<ESerializationVersion>(
				static_cast<int>(ESerializationVersion::NEW_HORIZONS_SEIZE_INITIATIVE) - 1);
		ASSERT_NO_THROW(source->serialize(wire.oser));
		ASSERT_NO_THROW(receiver->serialize(wire.iser));
		EXPECT_EQ(receiver->getSeizeInitiativeState(), legacy ? SeizeInitiativeState{} : expected);
		std::vector<uint32_t> restoredDescriptorIds;
		for(const auto & unit : receiver->stacks)
		{
			ASSERT_NE(unit, nullptr);
			restoredDescriptorIds.push_back(unit->unitId());
		}
		EXPECT_EQ(restoredDescriptorIds, expectedDescriptorIds);
	}
	EXPECT_EQ(battle()->getSeizeInitiativeState(), expected);
}

TEST_F(NewHorizonsSeizeInitiativeTest, StaleCompletionReceiptRejectsWithoutMutation)
{
	prepare(); ASSERT_TRUE(issue());
	BattleNormalActivationCompleted receipt;
	receipt.battleID = BattleID(0); receipt.unitId = first->unitId();
	receipt.expected = battle()->getSeizeInitiativeState();
	complete(first);
	const auto completed = battle()->getSeizeInitiativeState();
	EXPECT_THROW(gameHandler->sendAndApply(receipt), std::runtime_error);
	EXPECT_EQ(battle()->getSeizeInitiativeState(), completed);
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

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsSeizeInitiativeTest, DetachedPaidOrderAndCompletionCopyIsolationUseSharedQueue)
{
	prepare();
	SeizeEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto prepared = branch->prepareHeroOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(prepared.has_value());
	ASSERT_TRUE(branch->beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	ASSERT_TRUE(branch->projectAcceptedHeroOrder(BattleSide::ATTACKER, HeroCommand::CHARGE, {}, *prepared));
	EXPECT_TRUE(branch->getSeizeInitiativeState().sides[0].triggered);
	EXPECT_FALSE(battle()->getSeizeInitiativeState().sides[0].triggered);
	branch->completeSeizeInitiativeActivation(first->unitId());
	EXPECT_TRUE(branch->getSeizeInitiativeState().completed(first->unitId()));
	EXPECT_FALSE(battle()->getSeizeInitiativeState().completed(first->unitId()));
	EXPECT_FALSE(branch->getSeizeInitiativeState().sides[0].awaitingAnchor);
}
#endif

TEST_F(NewHorizonsSeizeInitiativeTest, JointRapidAndSeizePreserveActiveFrontTruncationAndDistinctExistingSlots)
{
	prepare(true, true);
	ASSERT_TRUE(issue());
	const auto selected = battle()->getSeizeInitiativeState().sides[0].recipient;
	ASSERT_TRUE(selected == second->unitId() || selected == third->unitId());
	auto * delayed = selected == second->unitId() ? third : second;
	activate(delayed);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeWait(delayed)));
	ASSERT_TRUE(delayed->waited());
	activate(first);
	// Use the real typed compare/apply completion boundary, as the individual
	// Seize controls do. The still-current opportunity stays protected in UI.
	complete(first);
	ASSERT_FALSE(battle()->getSeizeInitiativeState().sides[0].awaitingAnchor);
	BattleRapidResponseStateChanged response;
	response.battleID = BattleID(0);
	response.side = BattleSide::ATTACKER;
	response.expected = battle()->getRapidResponseState(response.side);
	response.state = newHorizonsRapidResponse::capture(*battle(), response.side);
	gameHandler->sendAndApply(response);
	const auto pending = battle()->getRapidResponseState(response.side);
	ASSERT_EQ(pending.pendingUnitId, delayed->unitId());
	ASSERT_NE(pending.pendingUnitId, selected);

	std::vector<battle::Units> original, rapidOnly, seizeOnly, full, one;
	battle()->battleGetTurnOrder(original, 0, 1, 0, BattleSide::NONE, false, false);
	battle()->battleGetTurnOrder(rapidOnly, 0, 1, 0, BattleSide::NONE, true, false);
	battle()->battleGetTurnOrder(seizeOnly, 0, 1, 0, BattleSide::NONE, false, true);
	battle()->battleGetTurnOrder(full, 0, 1);
	battle()->battleGetTurnOrder(one, 1, 1);
	ASSERT_FALSE(original.empty());
	ASSERT_FALSE(rapidOnly.empty());
	ASSERT_FALSE(seizeOnly.empty());
	ASSERT_FALSE(full.empty());
	ASSERT_FALSE(one.empty());
	ASSERT_EQ(one.front().size(), 1u);
	ASSERT_GE(full.front().size(), 3u);
	EXPECT_EQ(full.front().front(), first);
	EXPECT_EQ(full.front()[1], delayed);
	EXPECT_EQ(one.front().front(), full.front().front());

	ASSERT_EQ(original.front().size(), seizeOnly.front().size());
	for(size_t index = 0; index < original.front().size(); ++index)
		if(battle()->battleGetOwner(original.front()[index]) == PlayerColor(1))
			EXPECT_EQ(seizeOnly.front()[index], original.front()[index])
				<< "Seize alone preserves exact enemy positions";
	// Rapid schedules its pending delayed slot before ordinary queue selection.
	// Seize permutes friendly slots in that queue; Rapid then retains priority.
	// Seize-only -> Rapid is a different composition, not the live contract.
	auto expected = rapidOnly.front();
	newHorizonsSeizeInitiative::reorder(*battle(), expected, true);
	const auto waiter = std::find(expected.begin(), expected.end(), delayed);
	ASSERT_NE(waiter, expected.end());
	expected.erase(waiter);
	expected.insert(std::next(expected.begin()), delayed);
	EXPECT_EQ(full.front(), expected) << "Only Rapid's documented promotion may cross enemy slots";
	const auto enemyIds = [this](const battle::Units & slots)
	{
		std::vector<uint32_t> result;
		for(const auto * unit : slots)
			if(battle()->battleGetOwner(unit) == PlayerColor(1))
				result.push_back(unit->unitId());
		return result;
	};
	EXPECT_EQ(enemyIds(full.front()), enemyIds(original.front()));
	const auto nextFriendly = std::find_if(std::next(full.front().begin(), 2), full.front().end(),
		[this](const auto * unit) { return battle()->battleGetOwner(unit) == PlayerColor(0); });
	ASSERT_NE(nextFriendly, full.front().end());
	EXPECT_EQ((*nextFriendly)->unitId(), selected);
	EXPECT_EQ(full.front().size(), original.front().size());
	for(const auto * unit : original.front())
		EXPECT_EQ(std::count(full.front().begin(), full.front().end(), unit), 1);
	for(const auto * unit : full.front())
		EXPECT_EQ(std::count(full.front().begin(), full.front().end(), unit), 1);
	EXPECT_EQ(battle()->getRapidResponseState(response.side), pending);
	EXPECT_EQ(battle()->getSeizeInitiativeState().sides[0].recipient, selected);
}
