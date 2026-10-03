/*
 * NewHorizonsTacticsDeploymentTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "BattleStartSnapshotFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/battle/BattleDeploymentState.h"
#include "../../../lib/battle/AccessibilityInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/callback/IGameInfoCallback.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <vstd/ContainerUtils.h>
#include <vcmi/Environment.h>

namespace
{
constexpr std::string_view battlecraftSkill = "new-horizons:battlecraft";
constexpr std::string_view tacticsPerk = "new-horizons:battlecraft.tactics";

class TacticsDeploymentEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit TacticsDeploymentEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsTacticsDeploymentTest : public BattleTestFixture
{
protected:
	struct WideDestination
	{
		BattleHex requested;
		BattleHex blockedTail;
		BattleHex shifted;
	};

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	bool offerContains(CGHeroInstance * hero, std::string_view perkId) const
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			if(std::ranges::any_of(offers, [perkId](const auto & offer)
			{
				return offer.selection.skillId == battlecraftSkill
					&& offer.selection.perkId == perkId;
			}))
				return true;
		}
		return false;
	}

	void acceptTacticsPerk(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(std::string(battlecraftSkill));
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, tacticsPerk));

		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId != battlecraftSkill
					|| offers[choice].selection.perkId != tacticsPerk)
					continue;

				gameHandler->levelUpHero(hero, offers, choice, seed, false);
				ASSERT_TRUE(hero->hasActivePerk(std::string(battlecraftSkill), std::string(tacticsPerk)));
				return;
			}
		}
		FAIL() << "Tactics never appeared in a legal Basic Battlecraft offer";
	}

	void selectTacticsPerk(CGHeroInstance * hero)
	{
		EXPECT_FALSE(offerContains(hero, tacticsPerk))
			<< "The Tactics perk requires Basic Battlecraft";
		acceptTacticsPerk(hero);
	}

	void startBattleWithTacticsDisabledLayout()
	{
		BattleSideArray<const CGHeroInstance *> heroes = {attackerSideHero, defenderSideHero};
		BattleSideArray<const CArmedInstance *> armies = {attackerSideHero, defenderSideHero};
		const int3 tile(4, 4, 0);
		const auto terrain = gameState()->getTile(tile)->getTerrainID();
		auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
		layout.tacticsAllowed = false;

		const std::string battlefieldName = "core:sand_shore";
		const auto battlefieldIdentifier = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "battlefield", battlefieldName);
		ASSERT_TRUE(battlefieldIdentifier);
		BattleField battlefield(*battlefieldIdentifier);

		BattleStart start;
		start.battleID = BattleID(0);
		start.info = BattleInfo::setupBattle(gameState().get(), tile, terrain, battlefield,
			armies, heroes, layout, nullptr);
		gameHandler->sendAndApply(start);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		battle()->obstacles.clear();
	}

	static bool insideDeploymentZone(const BattleHex & hex, BattleSide side, int distance)
	{
		if(!hex.isAvailable())
			return false;
		if(side == BattleSide::ATTACKER)
			return hex.getX() > 0 && hex.getX() <= distance;
		if(side == BattleSide::DEFENDER)
			return hex.getX() < GameConstants::BFIELD_WIDTH - 1
				&& hex.getX() >= GameConstants::BFIELD_WIDTH - distance - 1;
		return false;
	}

	static bool footprintInsideDeploymentZone(const BattleHexArray & footprint, BattleSide side, int distance)
	{
		return !footprint.empty() && std::ranges::all_of(footprint, [side, distance](const BattleHex & hex)
		{
			return insideDeploymentZone(hex, side, distance);
		});
	}

	BattleHex findFreePosition(BattleSide side, bool doubleWide, bool withinDeploymentZone) const
	{
		const auto accessibility = battle()->getAccessibility();
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex position(static_cast<si16>(index));
			const auto & footprint = battle::Unit::getHexes(position, doubleWide, side);
			if(footprint.size() != (doubleWide ? 2u : 1u))
				continue;
			if(withinDeploymentZone != footprintInsideDeploymentZone(footprint, side, 3))
				continue;
			if(!accessibility.accessible(position, doubleWide, side))
				continue;
			if(std::ranges::any_of(footprint, [this](const BattleHex & hex)
			{
				return battle()->battleGetStackByPos(hex, true) != nullptr;
			}))
				continue;
			return position;
		}
		return BattleHex::INVALID;
	}

	std::optional<WideDestination> findWideDestinationShiftOutsideZone(const CStack * unit) const
	{
		const auto accessibility = battle()->getAccessibility(unit);
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex requested(static_cast<si16>(index));
			const auto & requestedFootprint = unit->getHexes(requested);
			if(!footprintInsideDeploymentZone(requestedFootprint, unit->unitSide(), 3)
				|| requested == unit->getPosition())
				continue;

			const BattleHex tail = unit->occupiedHex(requested);
			const BattleHex shifted = requested.cloneInDirection(unit->headDirection(), false);
			const auto & shiftedFootprint = unit->getHexes(shifted);
			if(shiftedFootprint.size() != 2
				|| footprintInsideDeploymentZone(shiftedFootprint, unit->unitSide(), 3)
				|| !accessibility.accessible(requested, unit)
				|| !accessibility.accessible(shifted, unit))
				continue;

			const auto footprintIsFree = [this](const BattleHexArray & footprint)
			{
				return std::ranges::none_of(footprint, [this](const BattleHex & hex)
				{
					return battle()->battleGetStackByPos(hex, true) != nullptr;
				});
			};
			if(!footprintIsFree(requestedFootprint) || !footprintIsFree(shiftedFootprint))
				continue;
			return WideDestination{requested, tail, shifted};
		}
		return {};
	}

	bool issueEnd(BattleSide actionSide, PlayerColor player)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), player,
			BattleAction::makeEndOFTacticPhase(actionSide));
	}

	bool issueMove(const CStack * unit, const BattleHex & destination)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(unit->unitSide()), BattleAction::makeMove(unit, destination));
	}

	CStack * findStack(BattleSide side) const
	{
		for(const auto * descriptor : battle()->battleGetAllStacks(false))
		{
			if(descriptor->unitSide() == side && descriptor->alive())
				return battle()->getStack(static_cast<int>(descriptor->unitId()), false);
		}
		return nullptr;
	}

};
}

TEST_F(NewHorizonsTacticsDeploymentTest, NoPerkCreatesNoIndependentDeploymentState)
{
	startGame();
	startBattle();

	EXPECT_EQ(battle()->getDeploymentState(), BattleDeploymentState{});
	EXPECT_EQ(battle()->tacticDistance, 0);
	EXPECT_EQ(battle()->tacticsSide, BattleSide::NONE);
}

TEST_F(NewHorizonsTacticsDeploymentTest, TacticsIsLegallyOfferedAtBasicAndSnapshotsAttackerRange)
{
	startGame();
	selectTacticsPerk(attackerSideHero);
	startBattle();

	const auto & deployment = battle()->getDeploymentState();
	ASSERT_TRUE(deployment.independent);
	EXPECT_EQ(deployment.distances[BattleSide::ATTACKER], 3);
	EXPECT_EQ(deployment.distances[BattleSide::DEFENDER], 0);
	EXPECT_FALSE(deployment.completed[BattleSide::ATTACKER]);
	EXPECT_EQ(deployment.activeSide(), BattleSide::ATTACKER);
	EXPECT_EQ(battle()->tacticsSide, BattleSide::ATTACKER);
	EXPECT_EQ(battle()->tacticDistance, 3);
}

TEST_F(NewHorizonsTacticsDeploymentTest, DefenderOnlyTacticsUsesTheDefenderProjection)
{
	startGame();
	selectTacticsPerk(defenderSideHero);
	startBattle();

	const auto & deployment = battle()->getDeploymentState();
	ASSERT_TRUE(deployment.independent);
	EXPECT_EQ(deployment.distances[BattleSide::ATTACKER], 0);
	EXPECT_EQ(deployment.distances[BattleSide::DEFENDER], 3);
	EXPECT_EQ(deployment.activeSide(), BattleSide::DEFENDER);
	EXPECT_EQ(battle()->tacticsSide, BattleSide::DEFENDER);
	EXPECT_EQ(battle()->tacticDistance, 3);
}

TEST_F(NewHorizonsTacticsDeploymentTest, BothArmiesResolveDeploymentSequentiallyBeforeFirstActivation)
{
	startGame();
	selectTacticsPerk(attackerSideHero);
	selectTacticsPerk(defenderSideHero);
	startBattle();

	const auto initialDeployment = battle()->getDeploymentState();
	ASSERT_TRUE(initialDeployment.independent);
	EXPECT_EQ(initialDeployment.distances[BattleSide::ATTACKER], 3);
	EXPECT_EQ(initialDeployment.distances[BattleSide::DEFENDER], 3);
	EXPECT_EQ(initialDeployment.activeSide(), BattleSide::ATTACKER);
	EXPECT_EQ(battle()->tacticDistance, 3);
	EXPECT_EQ(battle()->tacticsSide, BattleSide::ATTACKER);
	const int32_t roundBefore = battle()->getRound();
	const auto activationSerialBefore = battle()->getActivationSerial();
	const auto activationsBefore = server.stackActivations.size();
	const auto startedActionsBefore = server.startedActions.size();

	EXPECT_FALSE(issueEnd(BattleSide::ATTACKER, battle()->sideToPlayer(BattleSide::DEFENDER)))
		<< "The current side cannot submit its END action as the opponent";
	EXPECT_FALSE(issueEnd(BattleSide::DEFENDER, battle()->sideToPlayer(BattleSide::DEFENDER)))
		<< "The defender cannot skip the attacker's deployment phase";
	EXPECT_EQ(battle()->getDeploymentState(), initialDeployment);
	EXPECT_EQ(server.startedActions.size(), startedActionsBefore);

	ASSERT_TRUE(issueEnd(BattleSide::ATTACKER, battle()->sideToPlayer(BattleSide::ATTACKER)));
	const auto afterAttacker = battle()->getDeploymentState();
	EXPECT_TRUE(afterAttacker.completed[BattleSide::ATTACKER]);
	EXPECT_FALSE(afterAttacker.completed[BattleSide::DEFENDER]);
	EXPECT_EQ(afterAttacker.activeSide(), BattleSide::DEFENDER);
	EXPECT_EQ(battle()->tacticsSide, BattleSide::DEFENDER);
	EXPECT_EQ(battle()->tacticDistance, 3)
		<< "The scalar tactics view must mirror the still-pending defender phase";
	EXPECT_EQ(battle()->getRound(), roundBefore);
	EXPECT_EQ(battle()->getActivationSerial(), activationSerialBefore);
	EXPECT_EQ(server.stackActivations.size(), activationsBefore)
		<< "Ending the first side must not start combat or activate a creature";
	EXPECT_EQ(server.startedActions.size(), startedActionsBefore + 1);

	const auto afterFirstCompletion = battle()->getDeploymentState();
	const auto actionsBeforeStale = server.startedActions.size();
	EXPECT_FALSE(issueEnd(BattleSide::ATTACKER, battle()->sideToPlayer(BattleSide::ATTACKER)))
		<< "A repeated END from an already completed side is stale";
	EXPECT_EQ(battle()->getDeploymentState(), afterFirstCompletion);
	EXPECT_EQ(server.startedActions.size(), actionsBeforeStale);

	ASSERT_TRUE(issueEnd(BattleSide::DEFENDER, battle()->sideToPlayer(BattleSide::DEFENDER)));
	EXPECT_TRUE(battle()->getDeploymentState().completed[BattleSide::DEFENDER]);
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::NONE);
	EXPECT_EQ(battle()->tacticsSide, BattleSide::NONE);
	EXPECT_EQ(battle()->tacticDistance, 0);
	EXPECT_EQ(battle()->getRound(), 1);
	EXPECT_GT(battle()->getActivationSerial(), activationSerialBefore);
	EXPECT_GT(server.stackActivations.size(), activationsBefore)
		<< "Only the final END may begin the first ordinary Creature Activation";

	const auto completedDeployment = battle()->getDeploymentState();
	const auto actionsBeforeFinalStale = server.startedActions.size();
	EXPECT_FALSE(issueEnd(BattleSide::DEFENDER, battle()->sideToPlayer(BattleSide::DEFENDER)))
		<< "No END action is valid after all deployment opportunities are complete";
	EXPECT_EQ(battle()->getDeploymentState(), completedDeployment);
	EXPECT_EQ(server.startedActions.size(), actionsBeforeFinalStale);
}

TEST_F(NewHorizonsTacticsDeploymentTest, DisabledHeroPreferenceSuppressesThePerkSnapshot)
{
	startGame();
	selectTacticsPerk(attackerSideHero);
	ASSERT_TRUE(gameHandler->setTactics(attackerSideHero->id, false));
	EXPECT_FALSE(attackerSideHero->tacticFormationEnabled);
	startBattle();
	EXPECT_EQ(battle()->getDeploymentState(), BattleDeploymentState{});
	EXPECT_EQ(battle()->tacticDistance, 0);
}

TEST_F(NewHorizonsTacticsDeploymentTest, DisabledScenarioLayoutSuppressesThePerkSnapshot)
{
	startGame();
	selectTacticsPerk(attackerSideHero);
	startBattleWithTacticsDisabledLayout();
	EXPECT_EQ(battle()->getDeploymentState(), BattleDeploymentState{});
	EXPECT_EQ(battle()->tacticDistance, 0);
}

TEST_F(NewHorizonsTacticsDeploymentTest, DeploymentRejectsWrongSideOutOfZoneOccupiedAndWideShiftBeforeStartAction)
{
	startGame();
	selectTacticsPerk(attackerSideHero);
	startBattle();
	ASSERT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::ATTACKER);

	auto * defenderStack = findStack(BattleSide::DEFENDER);
	ASSERT_NE(defenderStack, nullptr);
	const auto startActionsBefore = server.startedActions.size();
	EXPECT_FALSE(issueMove(defenderStack, defenderStack->getPosition()))
		<< "A defender stack cannot move during the attacker's deployment phase";
	EXPECT_EQ(server.startedActions.size(), startActionsBefore);

	const auto pike = creatureByName("core:pikeman");
	const auto moverPosition = findFreePosition(BattleSide::ATTACKER, false, true);
	ASSERT_TRUE(moverPosition.isAvailable());
	auto * mover = addStack(BattleSide::ATTACKER, pike, moverPosition, 1);
	ASSERT_NE(mover, nullptr);

	// Prove the positive movement path before rejection-test fixtures add blockers around this stack.
	const auto initialReachability = battle()->getReachability(mover);
	const auto initialAccessibility = battle()->getAccessibility(mover);
	BattleHex legalDestination = BattleHex::INVALID;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(static_cast<si16>(index));
		const auto & footprint = mover->getHexes(candidate);
		if(candidate == moverPosition || !footprintInsideDeploymentZone(footprint, BattleSide::ATTACKER, 3)
			|| !initialAccessibility.accessible(candidate, mover) || !initialReachability.isReachable(candidate))
			continue;
		legalDestination = candidate;
		break;
	}
	ASSERT_TRUE(legalDestination.isAvailable());
	ASSERT_TRUE(issueMove(mover, legalDestination));
	EXPECT_EQ(mover->getPosition(), legalDestination);
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::ATTACKER)
		<< "An accepted deployment move must not finish the active side's deployment";

	const auto blockerPosition = findFreePosition(BattleSide::ATTACKER, false, true);
	ASSERT_TRUE(blockerPosition.isAvailable());
	auto * blocker = addStack(BattleSide::ATTACKER, pike, blockerPosition, 1);
	ASSERT_NE(blocker, nullptr);

	const auto outsidePosition = findFreePosition(BattleSide::ATTACKER, false, false);
	ASSERT_TRUE(outsidePosition.isAvailable());
	const auto moverStart = mover->getPosition();
	const auto beforeOutOfZone = server.startedActions.size();
	EXPECT_FALSE(issueMove(mover, outsidePosition));
	EXPECT_EQ(mover->getPosition(), moverStart);
	EXPECT_EQ(server.startedActions.size(), beforeOutOfZone)
		<< "Out-of-zone endpoints are rejected before StartAction is published";

	const auto beforeOccupied = server.startedActions.size();
	EXPECT_FALSE(issueMove(mover, blocker->getPosition()));
	EXPECT_EQ(mover->getPosition(), moverStart);
	EXPECT_EQ(server.startedActions.size(), beforeOccupied)
		<< "An occupied endpoint is rejected before StartAction is published";

	const auto widePosition = findFreePosition(BattleSide::ATTACKER, true, true);
	ASSERT_TRUE(widePosition.isAvailable());
	auto * hydra = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), widePosition, 1);
	ASSERT_NE(hydra, nullptr);
	ASSERT_TRUE(hydra->doubleWide());
	const auto wideDestination = findWideDestinationShiftOutsideZone(hydra);
	ASSERT_TRUE(wideDestination.has_value())
		<< "Fixture needs an empty double-wide destination whose normal shift would cross the Tactics boundary";
	ASSERT_EQ(wideDestination->blockedTail, hydra->occupiedHex(wideDestination->requested));
	ASSERT_NE(addStack(BattleSide::ATTACKER, pike, wideDestination->blockedTail, 1), nullptr);
	const auto accessibilityAfterTailBlock = battle()->getAccessibility(hydra);
	EXPECT_FALSE(accessibilityAfterTailBlock.accessible(wideDestination->requested, hydra));
	EXPECT_TRUE(accessibilityAfterTailBlock.accessible(wideDestination->shifted, hydra));
	EXPECT_FALSE(footprintInsideDeploymentZone(hydra->getHexes(wideDestination->shifted),
		BattleSide::ATTACKER, 3))
		<< "The preserved double-wide shift would place the rear footprint outside deployment range";
	const auto beforeWide = server.startedActions.size();
	const auto hydraStart = hydra->getPosition();
	EXPECT_FALSE(issueMove(hydra, wideDestination->requested));
	EXPECT_EQ(hydra->getPosition(), hydraStart);
	EXPECT_EQ(server.startedActions.size(), beforeWide)
		<< "Resolving a double-wide request must validate the shifted full footprint before publishing";

	ASSERT_TRUE(issueEnd(BattleSide::ATTACKER, battle()->sideToPlayer(BattleSide::ATTACKER)));
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::NONE)
		<< "An accepted Tactics move must still finish through the authoritative END request";
}

TEST_F(NewHorizonsTacticsDeploymentTest, StateAndTypedPhasePacketRoundTripWithLegacyFallbackAndDetachedIsolation)
{
	startGame();
	selectTacticsPerk(attackerSideHero);
	selectTacticsPerk(defenderSideHero);
	startBattle();
	const auto initial = battle()->getDeploymentState();
	ASSERT_TRUE(initial.independent);

	BattleStart outgoingStart;
	outgoingStart.battleID = BattleID(0);
	outgoingStart.info = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(outgoingStart.info, nullptr);
	CMemorySerializer startWire;
	startWire.oser.version = ESerializationVersion::CURRENT;
	startWire.iser.version = ESerializationVersion::CURRENT;
	startWire.iser.cb = gameState().get();
	startWire.oser & outgoingStart;
	BattleStart restoredStart;
	startWire.iser & restoredStart;
	ASSERT_NE(restoredStart.info, nullptr);
	EXPECT_EQ(restoredStart.info->getDeploymentState(), initial);
	EXPECT_EQ(restoredStart.info->tacticsSide, BattleSide::ATTACKER);
	EXPECT_EQ(restoredStart.info->tacticDistance, 3);

	CMemorySerializer oldStartWriter;
	oldStartWriter.oser.version = ESerializationVersion::NEW_HORIZONS_BATTLE_PLAN;
	EXPECT_THROW(oldStartWriter.oser & outgoingStart, std::runtime_error);
	EXPECT_TRUE(oldStartWriter.extractBuffer().empty())
		<< "An older save/protocol must not silently erase independent deployment";

	auto afterAttacker = initial;
	afterAttacker.complete(BattleSide::ATTACKER);
	BattleDeploymentPhaseChanged update;
	update.battleID = BattleID(0);
	update.state = afterAttacker;
	CMemorySerializer phaseWire;
	phaseWire.oser.version = ESerializationVersion::CURRENT;
	phaseWire.iser.version = ESerializationVersion::CURRENT;
	phaseWire.oser & update;
	BattleDeploymentPhaseChanged restoredUpdate;
	phaseWire.iser & restoredUpdate;
	EXPECT_EQ(restoredUpdate.state, afterAttacker);
	EXPECT_EQ(restoredUpdate.battleID, BattleID(0));

	CMemorySerializer oldPhaseWriter;
	oldPhaseWriter.oser.version = ESerializationVersion::NEW_HORIZONS_BATTLE_PLAN;
	EXPECT_THROW(oldPhaseWriter.oser & update, std::runtime_error);
	EXPECT_TRUE(oldPhaseWriter.extractBuffer().empty());

	CMemorySerializer legacyStateWire;
	legacyStateWire.oser.version = ESerializationVersion::NEW_HORIZONS_BATTLE_PLAN;
	BattleDeploymentState emptyLegacyState;
	legacyStateWire.oser & emptyLegacyState;
	CMemorySerializer legacyStateReader(legacyStateWire.extractBuffer());
	legacyStateReader.iser.version = ESerializationVersion::NEW_HORIZONS_BATTLE_PLAN;
	BattleDeploymentState migratedState = initial;
	legacyStateReader.iser & migratedState;
	EXPECT_EQ(migratedState, BattleDeploymentState{})
		<< "A legacy reader must default the absent deployment extension to the original single-phase state";

	TacticsDeploymentEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(&environment, callback);
	EXPECT_EQ(projected.getDeploymentState(), initial);
	BattleStatePackVisitor projectedVisitor(projected);
	restoredUpdate.visitTyped(projectedVisitor);
	EXPECT_EQ(projected.getDeploymentState(), afterAttacker);
	EXPECT_EQ(battle()->getDeploymentState(), initial)
		<< "Applying a deployment phase packet to a detached AI branch must not mutate the live battle";
}
