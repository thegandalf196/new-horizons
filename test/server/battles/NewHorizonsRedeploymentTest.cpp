/*
 * NewHorizonsRedeploymentTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in the main folder
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "BattleStartSnapshotFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleDeploymentState.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/battle/AccessibilityInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/callback/IGameInfoCallback.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/filesystem/ResourcePath.h"
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
#include <string>
#include <string_view>
#include <utility>

#include <vstd/ContainerUtils.h>
#include <vcmi/Environment.h>

namespace
{
constexpr std::string_view battlecraftSkill = "new-horizons:battlecraft";
constexpr std::string_view tacticsPerk = "new-horizons:battlecraft.tactics";
constexpr std::string_view entrenchPerk = "new-horizons:battlecraft.entrench";
constexpr std::string_view redeploymentPerk = "new-horizons:battlecraft.redeployment";

bool activateRedeployment(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(battlecraftSkill)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == redeploymentPerk;
	});
	if(found == perks.end())
		return false;
	(*found)["effect"]["status"].String() = "active";
	return true;
}

class RedeploymentEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit RedeploymentEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsRedeploymentTest : public BattleTestFixture
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

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!activateRedeployment(perkRules))
			throw std::runtime_error("Missing Redeployment from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void selectPerk(CGHeroInstance * hero, int rank, std::string_view perkId)
	{
		const int decoded = SecondarySkill::decode(std::string(battlecraftSkill));
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);

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
					|| offers[choice].selection.perkId != perkId)
					continue;

				gameHandler->levelUpHero(hero, offers, choice, seed, false);
				ASSERT_TRUE(hero->hasActivePerk(std::string(battlecraftSkill), std::string(perkId)));
				return;
			}
		}
		FAIL() << perkId << " never appeared in a legal Battlecraft perk offer";
	}

	void selectRedeployment(CGHeroInstance * hero, std::string_view basicPerk)
	{
		selectPerk(hero, MasteryLevel::BASIC, basicPerk);
		selectPerk(hero, MasteryLevel::ADVANCED, redeploymentPerk);
		ASSERT_TRUE(hero->hasActivePerk(std::string(battlecraftSkill), std::string(redeploymentPerk)));
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

	BattleHex findFreePosition(BattleSide side, bool doubleWide, int distance, bool inside) const
	{
		const auto accessibility = battle()->getAccessibility();
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex position(static_cast<si16>(index));
			const auto & footprint = battle::Unit::getHexes(position, doubleWide, side);
			if(footprint.size() != (doubleWide ? 2u : 1u)
				|| inside != footprintInsideDeploymentZone(footprint, side, distance)
				|| !accessibility.accessible(position, doubleWide, side))
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

	BattleHex findLegalDestination(const CStack * unit, int distance) const
	{
		const auto reachability = battle()->getReachability(unit);
		const auto accessibility = battle()->getAccessibility(unit);
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex candidate(static_cast<si16>(index));
			const auto & footprint = unit->getHexes(candidate);
			if(candidate == unit->getPosition()
				|| !footprintInsideDeploymentZone(footprint, unit->unitSide(), distance)
				|| !accessibility.accessible(candidate, unit) || !reachability.isReachable(candidate))
				continue;
			return candidate;
		}
		return BattleHex::INVALID;
	}

	std::optional<WideDestination> findWideDestinationShiftOutsideZone(const CStack * unit, int distance) const
	{
		const auto accessibility = battle()->getAccessibility(unit);
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex requested(static_cast<si16>(index));
			const auto & requestedFootprint = unit->getHexes(requested);
			if(!footprintInsideDeploymentZone(requestedFootprint, unit->unitSide(), distance)
				|| requested == unit->getPosition())
				continue;

			const BattleHex tail = unit->occupiedHex(requested);
			const BattleHex shifted = requested.cloneInDirection(unit->headDirection(), false);
			const auto & shiftedFootprint = unit->getHexes(shifted);
			if(shiftedFootprint.size() != 2
				|| footprintInsideDeploymentZone(shiftedFootprint, unit->unitSide(), distance)
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

	CStack * findStack(BattleSide side) const
	{
		for(const auto * descriptor : battle()->battleGetAllStacks(false))
		{
			if(descriptor->unitSide() == side && descriptor->alive())
				return battle()->getStack(static_cast<int>(descriptor->unitId()), false);
		}
		return nullptr;
	}

	bool issueEnd(BattleSide side, PlayerColor player)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), player,
			BattleAction::makeEndOFTacticPhase(side));
	}

	bool issueMove(const CStack * unit, const BattleHex & destination)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(unit->unitSide()), BattleAction::makeMove(unit, destination));
	}

	std::size_t queueActivations() const
	{
		return static_cast<std::size_t>(server.stackActivations.size());
	}
};
}

TEST_F(NewHorizonsRedeploymentTest, BothInitialPhasesPrecedeOneFinalWalkAndAnExplicitPass)
{
	startGame();
	selectRedeployment(attackerSideHero, tacticsPerk);
	selectRedeployment(defenderSideHero, tacticsPerk);
	startBattle();

	const auto initial = battle()->getDeploymentState();
	ASSERT_TRUE(initial.independent);
	EXPECT_EQ(initial.distances[BattleSide::ATTACKER], 3);
	EXPECT_EQ(initial.distances[BattleSide::DEFENDER], 3);
	EXPECT_EQ(initial.finalRelocationDistances[BattleSide::ATTACKER], 3);
	EXPECT_EQ(initial.finalRelocationDistances[BattleSide::DEFENDER], 3);
	EXPECT_EQ(initial.activeSide(), BattleSide::ATTACKER);
	EXPECT_FALSE(initial.isFinalRelocation());
	EXPECT_EQ(initial.activeDistance(), 3);

	const int32_t roundBefore = battle()->getRound();
	const auto activationSerialBefore = battle()->getActivationSerial();
	const auto activationsBefore = queueActivations();
	const auto actionsBefore = server.startedActions.size();

	ASSERT_TRUE(issueEnd(BattleSide::ATTACKER, battle()->sideToPlayer(BattleSide::ATTACKER)));
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::DEFENDER);
	EXPECT_FALSE(battle()->getDeploymentState().isFinalRelocation());
	EXPECT_EQ(server.startedActions.size(), actionsBefore + 1);
	EXPECT_EQ(battle()->getRound(), roundBefore);
	EXPECT_EQ(battle()->getActivationSerial(), activationSerialBefore);
	EXPECT_EQ(queueActivations(), activationsBefore);

	ASSERT_TRUE(issueEnd(BattleSide::DEFENDER, battle()->sideToPlayer(BattleSide::DEFENDER)));
	const auto & firstFinal = battle()->getDeploymentState();
	EXPECT_TRUE(firstFinal.completed[BattleSide::ATTACKER]);
	EXPECT_TRUE(firstFinal.completed[BattleSide::DEFENDER]);
	EXPECT_EQ(firstFinal.activeSide(), BattleSide::ATTACKER);
	EXPECT_TRUE(firstFinal.isFinalRelocation());
	EXPECT_EQ(firstFinal.activeDistance(), 3);
	EXPECT_EQ(battle()->getRound(), roundBefore);
	EXPECT_EQ(battle()->getActivationSerial(), activationSerialBefore);
	EXPECT_EQ(queueActivations(), activationsBefore);

	auto * mover = findStack(BattleSide::ATTACKER);
	auto * defenderStack = findStack(BattleSide::DEFENDER);
	ASSERT_NE(mover, nullptr);
	ASSERT_NE(defenderStack, nullptr);
	const auto legalDestination = findLegalDestination(mover, firstFinal.activeDistance());
	ASSERT_TRUE(legalDestination.isAvailable()) << "The final Tactics+Redeployment zone must offer a legal destination";
	ASSERT_TRUE(issueMove(mover, legalDestination));
	EXPECT_EQ(mover->getPosition(), legalDestination);
	const auto afterAttackerWalk = battle()->getDeploymentState();
	EXPECT_TRUE(afterAttackerWalk.finalRelocationCompleted[BattleSide::ATTACKER]);
	EXPECT_FALSE(afterAttackerWalk.finalRelocationCompleted[BattleSide::DEFENDER]);
	EXPECT_EQ(afterAttackerWalk.activeSide(), BattleSide::DEFENDER);
	EXPECT_TRUE(afterAttackerWalk.isFinalRelocation());
	EXPECT_EQ(afterAttackerWalk.activeDistance(), 3);
	EXPECT_EQ(battle()->getRound(), roundBefore)
		<< "An accepted first-side relocation must not begin the first Creature Activation";
	EXPECT_EQ(battle()->getActivationSerial(), activationSerialBefore);
	EXPECT_EQ(queueActivations(), activationsBefore);

	const auto actionsBeforeSecondMove = server.startedActions.size();
	EXPECT_FALSE(issueMove(mover, mover->getPosition()))
		<< "The first army cannot take a second relocation while the defender's final phase is active";
	EXPECT_EQ(server.startedActions.size(), actionsBeforeSecondMove);
	EXPECT_EQ(battle()->getDeploymentState(), afterAttackerWalk);

	ASSERT_TRUE(issueEnd(BattleSide::DEFENDER, battle()->sideToPlayer(BattleSide::DEFENDER)))
		<< "END is an explicit pass for the defender's unused final relocation";
	const auto completed = battle()->getDeploymentState();
	EXPECT_TRUE(completed.finalRelocationCompleted[BattleSide::ATTACKER]);
	EXPECT_TRUE(completed.finalRelocationCompleted[BattleSide::DEFENDER]);
	EXPECT_EQ(completed.activeSide(), BattleSide::NONE);
	EXPECT_EQ(battle()->getRound(), 1);
	EXPECT_GT(battle()->getActivationSerial(), activationSerialBefore);
	EXPECT_GT(queueActivations(), activationsBefore)
		<< "Only the last deployment completion may start the ordinary turn queue";
}

TEST_F(NewHorizonsRedeploymentTest, RedeploymentWithoutTacticsOffersTheNormalOneRowRange)
{
	startGame();
	selectRedeployment(attackerSideHero, entrenchPerk);
	startBattle();

	const auto initial = battle()->getDeploymentState();
	ASSERT_TRUE(initial.independent);
	EXPECT_EQ(initial.initialActiveSide(), BattleSide::NONE);
	EXPECT_EQ(initial.finalRelocationDistances[BattleSide::ATTACKER], 1);
	EXPECT_EQ(initial.finalRelocationDistances[BattleSide::DEFENDER], 0);
	EXPECT_EQ(initial.activeSide(), BattleSide::ATTACKER);
	ASSERT_TRUE(initial.isFinalRelocation());
	EXPECT_EQ(initial.activeDistance(), 1);

	auto * mover = findStack(BattleSide::ATTACKER);
	ASSERT_NE(mover, nullptr);
	const auto legalDestination = findLegalDestination(mover, 1);
	ASSERT_TRUE(legalDestination.isAvailable()) << "The normal one-row deployment area must permit one legal relocation";
	ASSERT_TRUE(issueMove(mover, legalDestination));
	EXPECT_EQ(mover->getPosition(), legalDestination);
	EXPECT_TRUE(battle()->getDeploymentState().finalRelocationCompleted[BattleSide::ATTACKER]);
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::NONE);
	EXPECT_EQ(battle()->getRound(), 1);
	EXPECT_GT(battle()->getActivationSerial(), 0);
	EXPECT_NE(battle()->battleActiveUnit(), nullptr);
}

TEST_F(NewHorizonsRedeploymentTest, RejectedFinalMovesDoNotSpendTheAllowance)
{
	startGame();
	selectRedeployment(attackerSideHero, tacticsPerk);
	startBattle();
	ASSERT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::ATTACKER);
	ASSERT_TRUE(issueEnd(BattleSide::ATTACKER, battle()->sideToPlayer(BattleSide::ATTACKER)));
	ASSERT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::ATTACKER);
	ASSERT_TRUE(battle()->getDeploymentState().isFinalRelocation());

	const int distance = battle()->getDeploymentState().activeDistance();
	const auto moverPosition = findFreePosition(BattleSide::ATTACKER, false, distance, true);
	ASSERT_TRUE(moverPosition.isAvailable());
	auto * mover = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), moverPosition, 1);
	ASSERT_NE(mover, nullptr);
	const auto blockerPosition = findFreePosition(BattleSide::ATTACKER, false, distance, true);
	ASSERT_TRUE(blockerPosition.isAvailable());
	auto * blocker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), blockerPosition, 1);
	ASSERT_NE(blocker, nullptr);
	const auto hydraPosition = findFreePosition(BattleSide::ATTACKER, true, distance, true);
	ASSERT_TRUE(hydraPosition.isAvailable());
	auto * hydra = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"),
		hydraPosition, 1);
	ASSERT_NE(hydra, nullptr);
	ASSERT_TRUE(hydra->doubleWide());
	auto * defenderStack = findStack(BattleSide::DEFENDER);
	ASSERT_NE(defenderStack, nullptr);

	const auto stateBefore = battle()->getDeploymentState();
	const auto roundBefore = battle()->getRound();
	const auto activationBefore = battle()->getActivationSerial();
	const auto startsBeforeWrongSide = server.startedActions.size();
	EXPECT_FALSE(issueMove(defenderStack, defenderStack->getPosition()));
	EXPECT_EQ(server.startedActions.size(), startsBeforeWrongSide)
		<< "A wrong-side request must be rejected before a WALK is published";
	EXPECT_EQ(battle()->getDeploymentState(), stateBefore);

	const auto outsidePosition = findFreePosition(BattleSide::ATTACKER, false, distance, false);
	ASSERT_TRUE(outsidePosition.isAvailable());
	const auto moverStart = mover->getPosition();
	const auto startsBeforeOutside = server.startedActions.size();
	EXPECT_FALSE(issueMove(mover, outsidePosition));
	EXPECT_EQ(mover->getPosition(), moverStart);
	EXPECT_EQ(server.startedActions.size(), startsBeforeOutside);
	EXPECT_EQ(battle()->getDeploymentState(), stateBefore);

	const auto startsBeforeOccupied = server.startedActions.size();
	EXPECT_FALSE(issueMove(mover, blocker->getPosition()));
	EXPECT_EQ(mover->getPosition(), moverStart);
	EXPECT_EQ(server.startedActions.size(), startsBeforeOccupied);
	EXPECT_EQ(battle()->getDeploymentState(), stateBefore);

	const auto wideDestination = findWideDestinationShiftOutsideZone(hydra, distance);
	ASSERT_TRUE(wideDestination.has_value())
		<< "Fixture needs a request whose double-wide destination shift would leave the final deployment zone";
	ASSERT_EQ(wideDestination->blockedTail, hydra->occupiedHex(wideDestination->requested));
	ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), wideDestination->blockedTail, 1), nullptr);
	const auto startsBeforeWide = server.startedActions.size();
	const auto hydraStart = hydra->getPosition();
	EXPECT_FALSE(issueMove(hydra, wideDestination->requested));
	EXPECT_EQ(hydra->getPosition(), hydraStart);
	EXPECT_EQ(server.startedActions.size(), startsBeforeWide)
		<< "The shifted full footprint must be checked before publishing a move";
	EXPECT_EQ(battle()->getDeploymentState(), stateBefore);

	static_cast<void>(issueMove(mover, mover->getPosition()));
	EXPECT_EQ(mover->getPosition(), moverStart);
	EXPECT_EQ(battle()->getDeploymentState(), stateBefore)
		<< "An accepted or rejected no-op cannot consume Redeployment";
	EXPECT_EQ(battle()->getRound(), roundBefore);
	EXPECT_EQ(battle()->getActivationSerial(), activationBefore);
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::ATTACKER);
	EXPECT_FALSE(battle()->getDeploymentState().finalRelocationCompleted[BattleSide::ATTACKER]);
}

TEST_F(NewHorizonsRedeploymentTest, FinalRelocationSavePacketAndDetachedProjectionPreserveBothSides)
{
	startGame();
	selectRedeployment(attackerSideHero, tacticsPerk);
	selectRedeployment(defenderSideHero, tacticsPerk);
	startBattle();
	const auto initial = battle()->getDeploymentState();
	ASSERT_TRUE(initial.independent);
	ASSERT_EQ(initial.finalRelocationDistances[BattleSide::ATTACKER], 3);
	ASSERT_EQ(initial.finalRelocationDistances[BattleSide::DEFENDER], 3);

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

	CMemorySerializer oldStartWriter;
	oldStartWriter.oser.version = ESerializationVersion::BATTLE_DEPLOYMENT_PHASES;
	EXPECT_THROW(oldStartWriter.oser & outgoingStart, std::runtime_error);
	EXPECT_TRUE(oldStartWriter.extractBuffer().empty())
		<< "An older save/protocol must reject final relocation state before writing bytes";

	auto afterAttackerEnd = initial;
	afterAttackerEnd.complete(BattleSide::ATTACKER);
	BattleDeploymentPhaseChanged update;
	update.battleID = BattleID(0);
	update.state = afterAttackerEnd;
	CMemorySerializer phaseWire;
	phaseWire.oser.version = ESerializationVersion::CURRENT;
	phaseWire.iser.version = ESerializationVersion::CURRENT;
	phaseWire.oser & update;
	BattleDeploymentPhaseChanged restoredUpdate;
	phaseWire.iser & restoredUpdate;
	EXPECT_EQ(restoredUpdate.state, afterAttackerEnd);
	EXPECT_EQ(restoredUpdate.battleID, BattleID(0));

	CMemorySerializer oldPhaseWriter;
	oldPhaseWriter.oser.version = ESerializationVersion::BATTLE_DEPLOYMENT_PHASES;
	EXPECT_THROW(oldPhaseWriter.oser & update, std::runtime_error);
	EXPECT_TRUE(oldPhaseWriter.extractBuffer().empty())
		<< "An older packet writer must reject final relocation state before writing bytes";

	RedeploymentEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(&environment, callback);
	EXPECT_EQ(projected.getDeploymentState(), initial);
	BattleStatePackVisitor projectedVisitor(projected);
	restoredUpdate.visitTyped(projectedVisitor);
	EXPECT_EQ(projected.getDeploymentState(), afterAttackerEnd);
	EXPECT_EQ(battle()->getDeploymentState(), initial)
		<< "Applying a final relocation phase packet to a detached projection cannot mutate live state";
}
