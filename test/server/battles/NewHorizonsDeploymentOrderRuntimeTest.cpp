/*
 * NewHorizonsDeploymentOrderRuntimeTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "BattleStartSnapshotFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleDeploymentState.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <vstd/ContainerUtils.h>

namespace
{
constexpr std::string_view battlecraftSkill = "new-horizons:battlecraft";
constexpr std::string_view entrenchPerk = "new-horizons:battlecraft.entrench";
constexpr std::string_view tacticsPerk = "new-horizons:battlecraft.tactics";
constexpr std::string_view passingLinesPerk = "new-horizons:battlecraft.passingLines";
constexpr std::string_view redeploymentPerk = "new-horizons:battlecraft.redeployment";
constexpr std::string_view grandTacticsPerk = "new-horizons:battlecraft.grandTactics";

std::optional<std::string> activateGrandTactics(JsonNode & rules, bool & overriddenForFixture)
{
	overriddenForFixture = false;
	auto & perks = rules["skills"][std::string(battlecraftSkill)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == grandTacticsPerk;
	});
	if(found == perks.end())
		return {};

	const auto productionStatus = (*found)["effect"]["status"].String();
	if(productionStatus == "planned")
	{
		overriddenForFixture = true;
		(*found)["effect"]["status"].String() = "active";
	}
	else if(productionStatus != "active")
	{
		return {};
	}
	return productionStatus;
}

class NewHorizonsDeploymentOrderRuntimeTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		bool overriddenForFixture = false;
		const auto productionStatus = activateGrandTactics(perkRules, overriddenForFixture);
		if(!productionStatus)
			throw std::runtime_error("Grand Tactics must be active or planned in the New Horizons perk registry");
		RecordProperty("grand_tactics_registry_status", *productionStatus);
		RecordProperty("grand_tactics_test_override_applied", overriddenForFixture ? "true" : "false");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void selectPerk(CGHeroInstance * hero, int rank, std::string_view perkId)
	{
		const int decoded = SecondarySkill::decode(std::string(battlecraftSkill));
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);

		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
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

	void selectGrandTactics(CGHeroInstance * hero)
	{
		selectPerk(hero, MasteryLevel::BASIC, entrenchPerk);
		selectPerk(hero, MasteryLevel::ADVANCED, passingLinesPerk);
		selectPerk(hero, MasteryLevel::EXPERT, grandTacticsPerk);
	}

	void selectBasicTactics(CGHeroInstance * hero)
	{
		selectPerk(hero, MasteryLevel::BASIC, tacticsPerk);
	}

	static bool insideDeploymentZone(const BattleHexArray & footprint, BattleSide side, int distance)
	{
		if(footprint.empty())
			return false;
		return std::ranges::all_of(footprint, [side, distance](const BattleHex & hex)
		{
			if(!hex.isAvailable())
				return false;
			if(side == BattleSide::ATTACKER)
				return hex.getX() > 0 && hex.getX() <= distance;
			if(side == BattleSide::DEFENDER)
				return hex.getX() < GameConstants::BFIELD_WIDTH - 1
					&& hex.getX() >= GameConstants::BFIELD_WIDTH - distance - 1;
			return false;
		});
	}

	CStack * findStack(BattleSide side) const
	{
		for(const auto * descriptor : battle()->battleGetAllStacks(false))
			if(descriptor->unitSide() == side && descriptor->alive())
				return battle()->getStack(static_cast<int>(descriptor->unitId()), false);
		return nullptr;
	}

	BattleHex findLegalDestination(const CStack * unit, BattleSide side, int distance) const
	{
		const auto reachability = battle()->getReachability(unit);
		const auto accessibility = battle()->getAccessibility(unit);
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex candidate(static_cast<si16>(index));
			if(candidate == unit->getPosition()
				|| !insideDeploymentZone(unit->getHexes(candidate), side, distance)
				|| !accessibility.accessible(candidate, unit)
				|| !reachability.isReachable(candidate))
				continue;
			return candidate;
		}
		return BattleHex::INVALID;
	}

	bool issueMove(const CStack * unit, const BattleHex & destination)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(unit->unitSide()), BattleAction::makeMove(unit, destination));
	}

	bool issueEnd(BattleSide side)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeEndOFTacticPhase(side));
	}
};
}

TEST_F(NewHorizonsDeploymentOrderRuntimeTest, AttackerGrandTacticsLetsDefenderMoveAndEndFirst)
{
	startGame();
	selectGrandTactics(attackerSideHero);
	selectBasicTactics(defenderSideHero);
	startBattle();

	const auto initial = battle()->getDeploymentState();
	ASSERT_TRUE(initial.independent);
	EXPECT_EQ(initial.distances[BattleSide::ATTACKER], 1);
	EXPECT_EQ(initial.distances[BattleSide::DEFENDER], 3);
	EXPECT_EQ(initial.initialFirstSide, BattleSide::DEFENDER);
	EXPECT_EQ(initial.activeSide(), BattleSide::DEFENDER);
	EXPECT_EQ(battle()->tacticsSide, BattleSide::DEFENDER);
	EXPECT_EQ(battle()->tacticDistance, 3);
	EXPECT_EQ(battle()->battleGetTacticsSide(), BattleSide::DEFENDER);
	EXPECT_EQ(battle()->battleTacticDist(), 3);
	EXPECT_EQ(battle()->battleGetTacticDist(), 3);

	BattleStart initialSnapshot;
	initialSnapshot.battleID = BattleID(0);
	initialSnapshot.info = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(initialSnapshot.info, nullptr);
	CMemorySerializer oldInitialOrderWriter;
	oldInitialOrderWriter.oser.version = ESerializationVersion::NEW_HORIZONS_PORTAL_SOURCE;
	EXPECT_THROW(oldInitialOrderWriter.oser & initialSnapshot, std::runtime_error);
	EXPECT_TRUE(oldInitialOrderWriter.extractBuffer().empty())
		<< "BattleStart must reject loss of the resolved non-default initial order before writing its payload";

	const auto roundBefore = battle()->getRound();
	const auto serialBefore = battle()->getActivationSerial();
	const auto activationsBefore = server.stackActivations.size();
	auto * attacker = findStack(BattleSide::ATTACKER);
	auto * defender = findStack(BattleSide::DEFENDER);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	const auto attackerPosition = attacker->getPosition();
	const auto defenderPosition = defender->getPosition();
	const auto attackerDestination = findLegalDestination(attacker, BattleSide::ATTACKER, 1);
	ASSERT_TRUE(attackerDestination.isAvailable())
		<< "The attacker must have a legal destination for its later one-row deployment phase";
	const auto actionsBeforeWrongSide = server.startedActions.size();

	EXPECT_FALSE(issueMove(attacker, attackerDestination))
		<< "Grand Tactics changes the side order, not the attacker's later legal deployment";
	EXPECT_FALSE(issueEnd(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getDeploymentState(), initial);
	EXPECT_EQ(attacker->getPosition(), attackerPosition);
	EXPECT_EQ(defender->getPosition(), defenderPosition);
	EXPECT_EQ(server.startedActions.size(), actionsBeforeWrongSide)
		<< "Wrong-side movement and END must be rejected before StartAction";
	EXPECT_EQ(server.stackActivations.size(), activationsBefore);

	const auto defenderDestination = findLegalDestination(defender, BattleSide::DEFENDER, 3);
	ASSERT_TRUE(defenderDestination.isAvailable());
	EXPECT_TRUE(battle()->isInTacticRange(defenderDestination, *defender));
	ASSERT_TRUE(issueMove(defender, defenderDestination));
	EXPECT_EQ(defender->getPosition(), defenderDestination);
	EXPECT_EQ(battle()->getDeploymentState(), initial)
		<< "A legal deployment move does not complete its side's phase";
	EXPECT_EQ(server.stackActivations.size(), activationsBefore);
	EXPECT_EQ(battle()->getRound(), roundBefore);
	EXPECT_EQ(battle()->getActivationSerial(), serialBefore);

	ASSERT_TRUE(issueEnd(BattleSide::DEFENDER));
	const auto afterDefender = battle()->getDeploymentState();
	EXPECT_TRUE(afterDefender.completed[BattleSide::DEFENDER]);
	EXPECT_FALSE(afterDefender.completed[BattleSide::ATTACKER]);
	EXPECT_EQ(afterDefender.activeSide(), BattleSide::ATTACKER);
	EXPECT_EQ(battle()->tacticsSide, BattleSide::ATTACKER);
	EXPECT_EQ(battle()->tacticDistance, 1);
	EXPECT_EQ(battle()->battleGetTacticsSide(), BattleSide::ATTACKER);
	EXPECT_EQ(battle()->battleTacticDist(), 1);
	EXPECT_EQ(battle()->battleGetTacticDist(), 1);
	EXPECT_EQ(server.stackActivations.size(), activationsBefore)
		<< "The first deployment END must not activate a creature";

	const auto laterAttackerDestination = findLegalDestination(attacker, BattleSide::ATTACKER, 1);
	ASSERT_TRUE(laterAttackerDestination.isAvailable());
	EXPECT_TRUE(battle()->isInTacticRange(laterAttackerDestination, *attacker));
	ASSERT_TRUE(issueMove(attacker, laterAttackerDestination));
	EXPECT_EQ(attacker->getPosition(), laterAttackerDestination);
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::ATTACKER);
	EXPECT_EQ(server.stackActivations.size(), activationsBefore);

	ASSERT_TRUE(issueEnd(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::NONE);
	EXPECT_EQ(battle()->getTacticsSide(), BattleSide::NONE);
	EXPECT_EQ(battle()->getTacticDist(), 0);
	EXPECT_EQ(battle()->getRound(), 1);
	EXPECT_GT(battle()->getActivationSerial(), serialBefore);
	EXPECT_GT(server.stackActivations.size(), activationsBefore)
		<< "Only the second initial deployment completion may start the first Creature Activation";
}

TEST_F(NewHorizonsDeploymentOrderRuntimeTest, BothGrandTacticsHoldersKeepTheNormalAttackerFirstOrder)
{
	startGame();
	selectGrandTactics(attackerSideHero);
	selectGrandTactics(defenderSideHero);
	startBattle();

	const auto & initial = battle()->getDeploymentState();
	ASSERT_TRUE(initial.independent);
	EXPECT_EQ(initial.distances[BattleSide::ATTACKER], 1);
	EXPECT_EQ(initial.distances[BattleSide::DEFENDER], 1);
	EXPECT_EQ(initial.initialFirstSide, BattleSide::ATTACKER);
	EXPECT_EQ(initial.activeSide(), BattleSide::ATTACKER);
	EXPECT_EQ(battle()->battleGetTacticsSide(), BattleSide::ATTACKER);
	EXPECT_EQ(battle()->battleTacticDist(), 1);
	EXPECT_TRUE(server.stackActivations.empty());
}

TEST_F(NewHorizonsDeploymentOrderRuntimeTest, GrandTacticsWithoutBasicTacticsGrantsOnlyOneDeploymentRow)
{
	startGame();
	selectGrandTactics(attackerSideHero);
	startBattle();

	const auto & initial = battle()->getDeploymentState();
	ASSERT_TRUE(initial.independent);
	EXPECT_EQ(initial.distances[BattleSide::ATTACKER], 1);
	EXPECT_EQ(initial.distances[BattleSide::DEFENDER], 0);
	EXPECT_EQ(initial.initialFirstSide, BattleSide::DEFENDER)
		<< "The resolved initial order is retained even when the defender has no phase to take";
	EXPECT_EQ(initial.activeSide(), BattleSide::ATTACKER);
	EXPECT_EQ(battle()->getTacticsSide(), BattleSide::ATTACKER);
	EXPECT_EQ(battle()->getTacticDist(), 1);
	EXPECT_TRUE(server.stackActivations.empty());
}

TEST_F(NewHorizonsDeploymentOrderRuntimeTest, GrandTacticsLeavesRedeploymentAfterBothInitialPhases)
{
	startGame();
	selectGrandTactics(attackerSideHero);
	selectBasicTactics(defenderSideHero);
	selectPerk(defenderSideHero, MasteryLevel::ADVANCED, redeploymentPerk);
	startBattle();

	const auto initial = battle()->getDeploymentState();
	ASSERT_EQ(initial.initialFirstSide, BattleSide::DEFENDER);
	ASSERT_EQ(initial.activeSide(), BattleSide::DEFENDER);
	EXPECT_EQ(initial.finalRelocationDistances[BattleSide::ATTACKER], 0);
	EXPECT_EQ(initial.finalRelocationDistances[BattleSide::DEFENDER], 3);

	const auto activationsBefore = server.stackActivations.size();
	ASSERT_TRUE(issueEnd(BattleSide::DEFENDER));
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::ATTACKER);
	EXPECT_FALSE(battle()->getDeploymentState().isFinalRelocation());
	ASSERT_TRUE(issueEnd(BattleSide::ATTACKER));
	const auto & finalRelocation = battle()->getDeploymentState();
	EXPECT_TRUE(finalRelocation.isFinalRelocation());
	EXPECT_EQ(finalRelocation.activeSide(), BattleSide::DEFENDER);
	EXPECT_EQ(finalRelocation.activeDistance(), 3);
	EXPECT_FALSE(finalRelocation.finalRelocationCompleted[BattleSide::ATTACKER]);
	EXPECT_FALSE(finalRelocation.finalRelocationCompleted[BattleSide::DEFENDER]);
	EXPECT_EQ(server.stackActivations.size(), activationsBefore)
		<< "The Grand Tactics initial-order change must not skip a later Redeployment phase";

	ASSERT_TRUE(issueEnd(BattleSide::DEFENDER));
	EXPECT_EQ(battle()->getDeploymentState().activeSide(), BattleSide::NONE);
	EXPECT_GT(server.stackActivations.size(), activationsBefore);
}
