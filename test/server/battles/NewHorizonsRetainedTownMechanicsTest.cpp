/*
 * NewHorizonsRetainedTownMechanicsTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/entities/building/CBuilding.h"
#include "../../../lib/entities/faction/CTown.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

namespace
{
class NewHorizonsRetainedTownMechanicsTest : public BattleTestFixture
{
protected:
	bool alliedThirdPlayer = false;
	CGTownInstance * town = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		if(alliedThirdPlayer)
		{
			loaded->howManyTeams = 2;
			loaded->players[0].team = TeamID(0);
			loaded->players[1].team = TeamID(1);
			loaded->players[2].team = TeamID(1);
		}
	}

	void prepare(FactionID faction, bool ally = false)
	{
		alliedThirdPlayer = ally;
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		ASSERT_GE(pikeman.getNum(), 0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(48, false).name("RetainedTownMechanics")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({3, 3, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{pikeman, 10}})
			.hero({43, 3, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{pikeman, 10}})
			.town({18, 24, 0}, faction, PlayerColor(1)).townGarrison({});
		if(ally)
			builder.playerActive(PlayerColor(2))
				.hero({43, 43, 0}, HeroTypeID(2), PlayerColor(2)).heroGarrison({{pikeman, 10}});
		startWithMap(std::move(builder));
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		town = expectAt<CGTownInstance>({18, 24, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_NE(town, nullptr);
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
	}

	void build(BuildingID building)
	{
		if(!town->hasBuilt(building))
			ASSERT_TRUE(gameHandler->buildStructure(town->id, building, true));
		ASSERT_TRUE(town->hasBuilt(building));
	}

	const CStack * pikemen(BattleSide side) const
	{
		const CreatureID type(CreatureID::decode("core:pikeman"));
		const auto result = battle()->getStacksIf([&](const CStack * stack)
		{
			return stack->unitSide() == side && stack->unitType()->getId() == type;
		});
		EXPECT_EQ(result.size(), 1u);
		return result.empty() ? nullptr : result.front();
	}

	void cancelBattle()
	{
		BattleCancelled cancelled;
		cancelled.battleID = battle()->battleID;
		gameHandler->sendAndApply(cancelled);
		ASSERT_TRUE(gameState()->currentBattles.empty());
	}

	void startTownBattle()
	{
		// Unlike the one-battle base fixture, these cases cancel and start a second
		// siege. Both the accepted packet and subsequent actions need its fresh ID.
		const int3 tile(4, 4, 0);
		const auto terrain = gameState()->getTile(tile)->getTerrainID();
		const std::string battlefieldName = "core:sand_shore";
		const auto identifier = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "battlefield", battlefieldName);
		ASSERT_TRUE(identifier.has_value());
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
		BattleStart start;
		start.battleID = gameState()->nextBattleID;
		const auto acceptedID = start.battleID;
		start.info = BattleInfo::setupBattle(gameState().get(), tile, terrain, BattleField(*identifier),
			{attackerSideHero, defenderSideHero}, {attackerSideHero, defenderSideHero}, layout, town);
		gameHandler->sendAndApply(start);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		ASSERT_EQ(battle()->battleID, acceptedID);
		if(!battle()->getDeploymentState().independent)
			battle()->tacticDistance = 0;
		battle()->obstacles.clear();
	}

	void activateDefender()
	{
		battle()->tacticDistance = 1;
		battle()->tacticsSide = BattleSide::ATTACKER;
		const auto endTactics = BattleAction::makeEndOFTacticPhase(BattleSide::ATTACKER);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->battleID,
			PlayerColor(0), endTactics));
		ASSERT_EQ(battle()->tacticDistance, 0);
		for(int actions = 0; actions < 16 && battle()->battleActiveUnit()
			&& battle()->battleActiveUnit()->unitSide() != BattleSide::DEFENDER; ++actions)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->battleID,
				battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
		}
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::DEFENDER);
	}
};
}

TEST_F(NewHorizonsRetainedTownMechanicsTest, BrotherhoodSuppliesTwoMoraleToSiegeDefendersNotAttackers)
{
	ASSERT_NO_FATAL_FAILURE(prepare(FactionID::CASTLE));
	ASSERT_NO_FATAL_FAILURE(build(BuildingID::TAVERN));
	town->setVisitingHero(defenderSideHero);
	ASSERT_NO_FATAL_FAILURE(startTownBattle());
	const auto * baselineDefender = pikemen(BattleSide::DEFENDER);
	const auto * baselineAttacker = pikemen(BattleSide::ATTACKER);
	ASSERT_NE(baselineDefender, nullptr);
	ASSERT_NE(baselineAttacker, nullptr);
	const auto previousTownMorale = town->valOfBonuses(BonusType::MORALE);
	const auto baselineMorale = battle()->battleGetMorale(baselineDefender);
	const auto attackerMorale = battle()->battleGetMorale(baselineAttacker);
	ASSERT_EQ(previousTownMorale, 1); // Tavern is replaced, not stacked, by Brotherhood.
	const auto expectedMorale = baselineDefender->moraleValWithBonus(2 - previousTownMorale);
	ASSERT_NO_FATAL_FAILURE(cancelBattle());
	ASSERT_NO_FATAL_FAILURE(build(BuildingID::SPECIAL_3));
	ASSERT_EQ(town->valOfBonuses(BonusType::MORALE), 2);
	ASSERT_NO_FATAL_FAILURE(startTownBattle());
	const auto * defender = pikemen(BattleSide::DEFENDER);
	const auto * attacker = pikemen(BattleSide::ATTACKER);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(attacker, nullptr);
	const BonusSourceID source(town->getTown()->buildings.at(BuildingID::SPECIAL_3)->getUniqueTypeID());
	const auto selector = Selector::source(BonusSource::TOWN_STRUCTURE, source)
		.And(Selector::type()(BonusType::MORALE));
	EXPECT_EQ(defender->valOfBonuses(selector), 2);
	EXPECT_EQ(attacker->valOfBonuses(selector), 0);
	EXPECT_EQ(battle()->battleGetMorale(defender), expectedMorale);
	EXPECT_GT(battle()->battleGetMorale(defender), baselineMorale);
	EXPECT_EQ(battle()->battleGetMorale(attacker), attackerMorale);
}

TEST_F(NewHorizonsRetainedTownMechanicsTest, CoverOfDarknessReshroudsEnemiesOnNewDayButPreservesAlliesAndOutsideRadius)
{
	ASSERT_NO_FATAL_FAILURE(prepare(FactionID::NECROPOLIS, true));
	ASSERT_EQ(gameState()->getPlayerRelations(PlayerColor(0), PlayerColor(1)), PlayerRelations::ENEMIES);
	ASSERT_EQ(gameState()->getPlayerRelations(PlayerColor(2), PlayerColor(1)), PlayerRelations::ALLIES);
	const auto covered = town->getSightCenter() + int3(8, 0, 0);
	const int3 outside(47, 47, 0);
	ASSERT_TRUE(gameState()->isInTheMap(covered));
	for(const auto player : {PlayerColor(0), PlayerColor(1), PlayerColor(2)})
		revealMap(player);
	const auto firstDay = gameState()->getCalendar().getCurrentDay();
	gameHandler->onNewTurn();
	ASSERT_EQ(gameState()->getCalendar().getCurrentDay(), firstDay + 1);
	ASSERT_TRUE(gameState()->isVisibleFor(covered, PlayerColor(0))); // no building: no reshrouding
	ASSERT_NO_FATAL_FAILURE(build(BuildingID::FORT));
	ASSERT_NO_FATAL_FAILURE(build(BuildingID::SPECIAL_1));
	ASSERT_EQ(town->valOfBonuses(BonusType::DARKNESS), 20);
	for(const auto player : {PlayerColor(0), PlayerColor(1), PlayerColor(2)})
		revealMap(player);
	const auto before = gameState()->getCalendar().getCurrentDay();
	gameHandler->onNewTurn();
	ASSERT_EQ(gameState()->getCalendar().getCurrentDay(), before + 1);
	EXPECT_FALSE(gameState()->isVisibleFor(covered, PlayerColor(0)));
	EXPECT_TRUE(gameState()->isVisibleFor(covered, PlayerColor(1)));
	EXPECT_TRUE(gameState()->isVisibleFor(covered, PlayerColor(2)));
	EXPECT_TRUE(gameState()->isVisibleFor(outside, PlayerColor(0)));
}

TEST_F(NewHorizonsRetainedTownMechanicsTest, EscapeTunnelChangesRejectedSiegeRetreatIntoAcceptedDefenderEscape)
{
	ASSERT_NO_FATAL_FAILURE(prepare(FactionID::STRONGHOLD));
	ASSERT_NO_FATAL_FAILURE(build(BuildingID::FORT));
	town->setVisitingHero(defenderSideHero);
	ASSERT_FALSE(town->hasBuilt(BuildingID::SPECIAL_1));
	ASSERT_NO_FATAL_FAILURE(startTownBattle());
	ASSERT_NO_FATAL_FAILURE(activateDefender());
	EXPECT_FALSE(battle()->battleCanFlee(PlayerColor(1)));
	const auto active = battle()->getActiveStackID();
	const auto firstBattleID = battle()->battleID;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(battle()->battleID,
		PlayerColor(1), BattleAction::makeRetreat(BattleSide::DEFENDER)));
	EXPECT_EQ(battle()->getActiveStackID(), active);
	ASSERT_NO_FATAL_FAILURE(cancelBattle());
	ASSERT_NO_FATAL_FAILURE(build(BuildingID::SPECIAL_1));
	ASSERT_NO_FATAL_FAILURE(startTownBattle());
	ASSERT_GT(battle()->battleID.getNum(), firstBattleID.getNum());
	ASSERT_NO_FATAL_FAILURE(activateDefender());
	ASSERT_TRUE(battle()->battleCanFlee(PlayerColor(1)));
	const auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
	gameHandler->queries->addQuery(query);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->battleID,
		PlayerColor(1), BattleAction::makeRetreat(BattleSide::DEFENDER)));
	ASSERT_TRUE(query->result.has_value());
	EXPECT_EQ(query->result->result, EBattleResult::ESCAPE);
	EXPECT_EQ(query->result->winner, BattleSide::ATTACKER);
}

TEST_F(NewHorizonsRetainedTownMechanicsTest, EscapeTunnelDoesNotOverrideShacklesOfWarRetreatProhibition)
{
	ASSERT_NO_FATAL_FAILURE(prepare(FactionID::STRONGHOLD));
	ASSERT_NO_FATAL_FAILURE(build(BuildingID::FORT));
	ASSERT_NO_FATAL_FAILURE(build(BuildingID::SPECIAL_1));
	town->setVisitingHero(defenderSideHero);
	const ArtifactID shackles(ArtifactID::decode("core:shacklesOfWar"));
	ASSERT_GE(shackles.getNum(), 0);
	giveArtifact(attackerSideHero, shackles, ArtifactPosition::MISC1);
	ASSERT_NO_FATAL_FAILURE(startTownBattle());
	ASSERT_NO_FATAL_FAILURE(activateDefender());
	EXPECT_FALSE(battle()->battleCanFlee(PlayerColor(1)));
	const auto active = battle()->getActiveStackID();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(battle()->battleID,
		PlayerColor(1), BattleAction::makeRetreat(BattleSide::DEFENDER)));
	EXPECT_EQ(battle()->getActiveStackID(), active);
}
