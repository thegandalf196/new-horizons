/*
 * NewHorizonsReserveTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/BattleChanges.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../include/vcmi/ServerCallback.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

namespace
{
constexpr auto battlecraftSkill = "new-horizons:battlecraft";
constexpr auto reservePerk = "new-horizons:battlecraft.reserve";
constexpr auto activationMovementKey = "activationMovementBonus";

bool setReserveStatus(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(battlecraftSkill)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == reservePerk;
	});
	if(found == perks.end())
		return false;
	(*found)["effect"]["status"].String() = "active";
	return true;
}

class ReserveTestEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ReserveTestEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsReserveTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setReserveStatus(perkRules))
			throw std::runtime_error("Missing Reserve from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			std::move(perkRules));
	}

	SecondarySkill battlecraft() const
	{
		const int decoded = SecondarySkill::decode(battlecraftSkill);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void selectReserve(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(battlecraft(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.skillId == battlecraftSkill
					&& candidate.selection.perkId == reservePerk;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(battlecraftSkill, reservePerk));
			return;
		}

		FAIL() << reservePerk << " never appeared in a legal Basic perk offer";
	}

	void removeStartingStacks()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	bool act(const CStack * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), battle()->battleGetOwner(stack), action);
	}

	void activate(const CStack * stack, BattleUnitTurnReason reason)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = reason;
		gameHandler->sendAndApply(activation);
	}

	std::size_t queueActivations(const CStack * stack) const
	{
		return static_cast<std::size_t>(std::ranges::count_if(server.stackActivations,
			[stack](const BattleSetActiveStack & activation)
		{
			return activation.stack == stack->unitId()
				&& activation.reason == BattleUnitTurnReason::TURN_QUEUE;
		}));
	}

	void applyUnitState(CStack * stack, battle::CUnitState & state)
	{
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state.save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}
};
}

TEST_F(NewHorizonsReserveTest, AcceptedWaitGrantsDelayedMovementAndAcceptedMoveConsumesIt)
{
	startGame();
	selectReserve(attackerSideHero);
	selectReserve(defenderSideHero);
	startBattle();
	removeStartingStacks();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(15, 5), 10);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	beginCombat();

	const auto * first = battle()->battleActiveUnit();
	ASSERT_NE(first, nullptr);
	auto * waiting = battle()->getStack(first->unitId());
	ASSERT_NE(waiting, nullptr);
	auto * intervening = waiting == attacker ? defender : attacker;
	const auto initialRange = waiting->getMovementRange(0);
	const auto initialInitiative = waiting->getInitiative(0);
	const auto initialQueueActivations = queueActivations(waiting);
	EXPECT_EQ(waiting->getActivationMovementBonus(), 0);
	EXPECT_EQ(waiting->getInitiative(0), initialInitiative)
		<< "The ordinary first activation receives no Reserve movement or initiative bonus";

	ASSERT_TRUE(act(waiting, BattleAction::makeWait(waiting)));
	EXPECT_TRUE(waiting->waiting);
	EXPECT_TRUE(waiting->waitedThisTurn);
	EXPECT_EQ(waiting->getActivationMovementBonus(), 0)
		<< "Waiting arms Reserve but does not grant its movement before the delayed activation";
	ASSERT_EQ(battle()->battleActiveUnit(), intervening);

	ASSERT_TRUE(act(intervening, BattleAction::makeDefend(intervening)));
	ASSERT_EQ(battle()->battleActiveUnit(), waiting);
	EXPECT_GT(queueActivations(waiting), initialQueueActivations);
	EXPECT_EQ(waiting->getActivationMovementBonus(), 2);
	EXPECT_EQ(waiting->getMovementRange(0), initialRange + 2);
	EXPECT_EQ(waiting->getInitiative(0), initialInitiative)
		<< "Reserve adds movement for this activation without changing turn order";

	const auto distances = battle()->battleGetDistances(waiting, waiting->getPosition());
	const auto available = battle()->battleGetAvailableHexes(waiting, false);
	BattleHex extraReach = BattleHex::INVALID;
	for(const auto candidate : available)
	{
		const auto distance = distances[candidate.toInt()];
		if(distance > static_cast<int>(initialRange) && distance <= static_cast<int>(initialRange + 2))
		{
			extraReach = candidate;
			break;
		}
	}
	ASSERT_TRUE(extraReach.isAvailable()) << "The added Speed should expose at least one additional legal hex";
	ASSERT_TRUE(act(waiting, BattleAction::makeMove(waiting, extraReach)));
	EXPECT_EQ(waiting->getPosition(), extraReach);
	EXPECT_EQ(waiting->getActivationMovementBonus(), 0)
		<< "Completing the delayed activation clears Reserve immediately";
}

TEST_F(NewHorizonsReserveTest, OnlyAnOwningHeroDelayedQueueActivationQualifies)
{
	startGame();
	selectReserve(defenderSideHero);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(battlecraftSkill, reservePerk));
	startBattle();
	removeStartingStacks();
	auto * unskilled = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 10);
	auto * skilled = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(15, 5), 10);
	ASSERT_NE(unskilled, nullptr);
	ASSERT_NE(skilled, nullptr);
	beginCombat();
	EXPECT_EQ(unskilled->getActivationMovementBonus(), 0);
	EXPECT_EQ(skilled->getActivationMovementBonus(), 0);

	// Exercise the authoritative transition on both controlling sides.  The
	// first stack has a waited state but no perk; the second has the Basic perk
	// and receives a real TURN_QUEUE transition only after its waited state.
	auto unskilledState = unskilled->acquireState();
	unskilledState->afterWait();
	applyUnitState(unskilled, *unskilledState);
	activate(unskilled, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(unskilled->getActivationMovementBonus(), 0)
		<< "The perk is checked on the unit's current controlling hero";

	activate(skilled, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(skilled->getActivationMovementBonus(), 0)
		<< "An ordinary queue activation without a preceding Wait is not delayed";
	auto skilledState = skilled->acquireState();
	skilledState->afterWait();
	applyUnitState(skilled, *skilledState);
	activate(skilled, BattleUnitTurnReason::MORALE);
	EXPECT_EQ(skilled->getActivationMovementBonus(), 0)
		<< "Morale grants a separate activation, not Reserve movement";
	activate(skilled, BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_EQ(skilled->getActivationMovementBonus(), 2);

	activate(skilled, BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_EQ(skilled->getActivationMovementBonus(), 2)
		<< "A spell continuation preserves the current delayed activation";
	activate(skilled, BattleUnitTurnReason::MORALE);
	EXPECT_EQ(skilled->getActivationMovementBonus(), 0);
	HeroOrderState secondWind;
	secondWind.command = HeroCommand::SECOND_WIND;
	secondWind.issuedRound = battle()->battleGetRound();
	secondWind.secondWindActive = true;
	secondWind.primaryTargetUnitId = skilled->unitId();
	battle()->getSide(BattleSide::DEFENDER).upsertOrder(secondWind);
	activate(skilled, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(skilled->getActivationMovementBonus(), 0)
		<< "Second Wind does not re-grant Reserve movement";
}

TEST_F(NewHorizonsReserveTest, UnitChangesAndBattleSnapshotPreserveReserveStateWithoutLeakingBranches)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(92), 10);
	ASSERT_NE(stack, nullptr);
	auto state = stack->acquireState();
	state->setActivationMovementBonus(2);
	applyUnitState(stack, *state);
	ASSERT_EQ(stack->getActivationMovementBonus(), 2);
	ASSERT_TRUE(battle()->hasReserveMovementState());

	const auto saved = stack->acquireState()->save();
	EXPECT_EQ(saved["state"][activationMovementKey].Integer(), 2);
	auto sourceCopy = stack->acquireState();
	auto branch = stack->acquireState();
	*branch = *sourceCopy;
	branch->setActivationMovementBonus(0);
	EXPECT_EQ(stack->getActivationMovementBonus(), 2)
		<< "Changing a detached state copy cannot mutate the authoritative stack";

	CMemorySerializer unitChangesWire;
	unitChangesWire.oser.version = ESerializationVersion::CURRENT;
	unitChangesWire.iser.version = ESerializationVersion::CURRENT;
	UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = saved;
	unitChangesWire.oser & update;
	unitChangesWire.iser.cb = gameState().get();
	UnitChanges decoded;
	unitChangesWire.iser & decoded;
	EXPECT_EQ(decoded.data["state"][activationMovementKey].Integer(), 2);
	CMemorySerializer olderUnitChanges;
	olderUnitChanges.oser.version = ESerializationVersion::NEW_HORIZONS_RALLY;
	EXPECT_THROW(olderUnitChanges.oser & update, std::runtime_error);
	EXPECT_TRUE(olderUnitChanges.extractBuffer().empty())
		<< "A nonzero activation bonus must fail before writing to a legacy unit update";

	CMemorySerializer snapshot;
	snapshot.oser.version = ESerializationVersion::CURRENT;
	snapshot.iser.version = ESerializationVersion::CURRENT;
	snapshot.oser & *battle();
	snapshot.iser.cb = gameState().get();
	BattleInfo restored(gameState().get());
	snapshot.iser & restored;
	ASSERT_TRUE(restored.hasReserveMovementState());
	// CStack's binary payload omits CUnitState health; verify only the appended Reserve sidecar.
	auto * restoredStack = restored.getStack(static_cast<int>(stack->unitId()), false);
	ASSERT_NE(restoredStack, nullptr);
	EXPECT_EQ(restoredStack->getActivationMovementBonus(), 2);

	CMemorySerializer olderSnapshot;
	olderSnapshot.oser.version = ESerializationVersion::NEW_HORIZONS_RALLY;
	EXPECT_THROW(olderSnapshot.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(olderSnapshot.extractBuffer().empty())
		<< "A nonzero activation bonus must fail before writing to a legacy snapshot";

	auto legacy = saved;
	legacy["state"].Struct().erase(activationMovementKey);
	auto restoredState = stack->acquireState();
	restoredState->load(legacy);
	EXPECT_EQ(restoredState->getActivationMovementBonus(), 0)
		<< "Older UnitChanges JSON without Reserve state defaults to zero";
	auto invalid = saved;
	invalid["state"][activationMovementKey] = JsonNode(-1);
	EXPECT_THROW(restoredState->load(invalid), std::runtime_error);
}

TEST_F(NewHorizonsReserveTest, ImmobilizationAndTimeStopStillSuppressReserveMovement)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(92), 10);
	ASSERT_NE(stack, nullptr);
	auto state = stack->acquireState();
	const auto normalRange = stack->getMovementRange(0);
	state->setActivationMovementBonus(2);
	applyUnitState(stack, *state);
	EXPECT_EQ(stack->getMovementRange(0), normalRange + 2);

	auto bind = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::BIND_EFFECT,
		BonusSource::OTHER, 0, BonusSourceID());
	stack->addNewBonus(bind);
	EXPECT_EQ(stack->getMovementRange(0), 0)
		<< "Reserve cannot override the existing immobilization guard";
	stack->removeBonus(bind);

	auto timeStop = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	stack->addNewBonus(timeStop);
	EXPECT_EQ(stack->getMovementRange(0), 0)
		<< "Reserve cannot override the Time Stop guard";
	stack->removeBonus(timeStop);
	EXPECT_EQ(stack->getMovementRange(0), normalRange + 2);
}

TEST_F(NewHorizonsReserveTest, HypotheticWaitKeepsReserveMovementBranchLocalThroughItsDelayedActivation)
{
	startGame();
	selectReserve(defenderSideHero);
	startBattle();
	removeStartingStacks();
	auto * original = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
	ASSERT_NE(original, nullptr);
	original->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(original->unitSide(), BattleSide::ATTACKER);
	ASSERT_EQ(battle()->battleGetOwner(original), PlayerColor(1));
	ASSERT_EQ(battle()->battleGetOwnerHero(original), defenderSideHero)
		<< "Reserve follows the current controller, not the unit's original army side";

	const auto initialRange = original->getMovementRange(0);
	const auto initialInitiative = original->getInitiative(0);
	ReserveTestEnvironment environment(gameState());
	// Evaluate the current controller's visible view, not the opposing player's
	// callback, which deliberately hides this hero's perk state.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	ASSERT_EQ(callback->battleGetOwnerHero(original), defenderSideHero);
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);

	branch->makeWait(original);
	auto projected = branch->getForUpdate(original->unitId());
	ASSERT_NE(projected, nullptr);
	EXPECT_TRUE(projected->waiting);
	EXPECT_TRUE(projected->waitedThisTurn);
	EXPECT_EQ(projected->getActivationMovementBonus(), 2);
	EXPECT_EQ(projected->getMovementRange(0), initialRange + 2)
		<< "The hypothetical Wait exposes Reserve movement in this candidate only";
	EXPECT_EQ(projected->getInitiative(0), initialInitiative)
		<< "The candidate's delayed movement does not change its initiative";
	EXPECT_EQ(parent->getForUpdate(original->unitId())->getActivationMovementBonus(), 0);
	EXPECT_EQ(sibling->getForUpdate(original->unitId())->getActivationMovementBonus(), 0);
	EXPECT_EQ(original->getActivationMovementBonus(), 0);
	EXPECT_EQ(original->getMovementRange(0), initialRange);

	branch->nextTurn(original->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	projected = branch->getForUpdate(original->unitId());
	EXPECT_EQ(projected->getActivationMovementBonus(), 2)
		<< "The delayed TURN_QUEUE activation preserves the branch's Reserve movement";
	EXPECT_EQ(projected->getInitiative(0), initialInitiative);
	branch->endFortuneActivation();
	EXPECT_EQ(branch->getForUpdate(original->unitId())->getActivationMovementBonus(), 0)
		<< "Closing the hypothetical activation clears only its branch state";
	EXPECT_EQ(parent->getForUpdate(original->unitId())->getActivationMovementBonus(), 0);
	EXPECT_EQ(sibling->getForUpdate(original->unitId())->getActivationMovementBonus(), 0);
	EXPECT_EQ(original->getActivationMovementBonus(), 0);
	EXPECT_EQ(original->getMovementRange(0), initialRange);
}
