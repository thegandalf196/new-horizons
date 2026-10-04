/*
 * NewHorizonsCastleStablesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/bonuses/CBonusSystemNode.h"
#include "../../../lib/entities/building/CBuilding.h"
#include "../../../lib/entities/faction/CTown.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/pathfinder/TurnInfo.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../server/CGameHandler.h"

#include <vector>

namespace
{
BonusSourceID stablesSource(const CGTownInstance & town)
{
	const auto & building = town.getTown()->buildings.at(BuildingID::SPECIAL_2);
	return BonusSourceID(building->getUniqueTypeID());
}

std::vector<const Bonus *> stablesBonuses(const CGHeroInstance & hero, const BonusSourceID & source)
{
	std::vector<const Bonus *> result;
	const auto bonuses = hero.getBonuses(Selector::source(BonusSource::TOWN_STRUCTURE, source)
		.And(Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementLand)));
	for(const auto & bonus : *bonuses)
	{
		if(bonus)
			result.push_back(bonus.get());
	}
	return result;
}

class NewHorizonsCastleStablesTest : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}
};
}

TEST_F(NewHorizonsCastleStablesTest, ResidentsReceiveDayStartMovementAndSharedRefillExpiresAndReacquires)
{
	const CreatureID zombie(CreatureID::decode("core:zombie"));
	const CreatureID archangel(CreatureID::decode("core:archangel"));
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NewHorizonsCastleStables")
		.playerActive(PlayerColor(0))
		.playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{zombie, 1}})
		.hero({7, 5, 0}, HeroTypeID(1), PlayerColor(0)).heroGarrison({{archangel, 1}})
		.hero({9, 5, 0}, HeroTypeID(2), PlayerColor(0)).heroGarrison({{pikeman, 1}})
		.town({18, 18, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({});
	startWithMap(std::move(builder));

	auto * town = expectAt<CGTownInstance>({18, 18, 0});
	auto * visitor = findHeroAt({5, 5, 0});
	auto * garrisonHero = findHeroAt({7, 5, 0});
	auto * outsider = findHeroAt({9, 5, 0});
	ASSERT_NE(town, nullptr);
	ASSERT_NE(visitor, nullptr);
	ASSERT_NE(garrisonHero, nullptr);
	ASSERT_NE(outsider, nullptr);
	ASSERT_EQ(town->getOwner(), PlayerColor(0));
	ASSERT_TRUE(town->getTown()->buildings.contains(BuildingID::SPECIAL_2));
	ASSERT_FALSE(town->hasBuilt(BuildingID::SPECIAL_2));
	EXPECT_FALSE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	EXPECT_TRUE(visitor->usesNewHorizonsMovement());
	EXPECT_TRUE(garrisonHero->usesNewHorizonsMovement());
	EXPECT_TRUE(outsider->usesNewHorizonsMovement());
	ASSERT_LT(visitor->getLowestCreatureSpeed(), garrisonHero->getLowestCreatureSpeed());
	EXPECT_EQ(visitor->movementPointsLimit(), 200);
	EXPECT_EQ(garrisonHero->movementPointsLimit(), 200);
	EXPECT_EQ(outsider->movementPointsLimit(), 200);

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	gameHandler.randomizer->setSeed(201);
	ASSERT_TRUE(gameHandler.buildStructure(town->id, BuildingID::SPECIAL_2, true));
	ASSERT_TRUE(town->hasBuilt(BuildingID::SPECIAL_2));
	const auto source = stablesSource(*town);
	EXPECT_TRUE(stablesBonuses(*visitor, source).empty());
	EXPECT_TRUE(stablesBonuses(*garrisonHero, source).empty());

	// Only heroes resident when the day starts receive the one-day modifier.
	town->setVisitingHero(visitor);
	town->setGarrisonedHero(garrisonHero);
	gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1);
	for(const auto * hero : {visitor, garrisonHero})
	{
		const auto bonuses = stablesBonuses(*hero, source);
		ASSERT_EQ(bonuses.size(), 1u);
		EXPECT_EQ(bonuses.front()->val, 20);
		EXPECT_EQ(bonuses.front()->valType, BonusValueType::PERCENT_TO_BASE);
		EXPECT_EQ(bonuses.front()->duration, BonusDuration::ONE_DAY);
		EXPECT_TRUE(Bonus::OneDay(bonuses.front()));
		EXPECT_EQ(hero->movementPointsLimit(), 240);
		EXPECT_EQ(hero->movementPointsRemaining(), 240);
		const auto currentTurn = hero->getTurnInfo(0);
		ASSERT_NE(currentTurn, nullptr);
		EXPECT_EQ(currentTurn->getMovePointsLimitLand(), 240);
	}
	EXPECT_TRUE(stablesBonuses(*outsider, source).empty());
	EXPECT_EQ(outsider->movementPointsLimit(), 200);
	EXPECT_EQ(outsider->movementPointsRemaining(), 200);

	// Leaving after the day-start grant keeps today's benefit. The shared
	// pathfinder forecast excludes this ONE_DAY bonus from tomorrow's pool.
	town->setVisitingHero(nullptr);
	EXPECT_EQ(stablesBonuses(*visitor, source).size(), 1u);
	EXPECT_EQ(visitor->movementPointsRemaining(), 240);
	const auto futureTurn = visitor->getTurnInfo(1);
	ASSERT_NE(futureTurn, nullptr);
	EXPECT_EQ(futureTurn->getMovePointsLimitLand(), 200);

	// Entering mid-day grants neither the Stables bonus nor the legacy flat 400.
	town->setVisitingHero(outsider);
	gameHandler.heroVisitCastle(town, outsider);
	EXPECT_TRUE(stablesBonuses(*outsider, source).empty());
	EXPECT_EQ(outsider->movementPointsLimit(), 200);
	EXPECT_EQ(outsider->movementPointsRemaining(), 200);

	// Save retains the active day bonus, including for the hero who has left.
	const auto visitorID = visitor->id;
	const auto garrisonHeroID = garrisonHero->id;
	const auto outsiderID = outsider->id;
	const auto townID = town->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	auto * restoredTown = restored.getTown(townID);
	auto * restoredVisitor = restored.getHero(visitorID);
	auto * restoredGarrisonHero = restored.getHero(garrisonHeroID);
	auto * restoredOutsider = restored.getHero(outsiderID);
	ASSERT_NE(restoredTown, nullptr);
	ASSERT_NE(restoredVisitor, nullptr);
	ASSERT_NE(restoredGarrisonHero, nullptr);
	ASSERT_NE(restoredOutsider, nullptr);
	EXPECT_EQ(restoredTown->getVisitingHero(), restoredOutsider);
	EXPECT_EQ(restoredTown->getGarrisonHero(), restoredGarrisonHero);
	EXPECT_EQ(stablesBonuses(*restoredVisitor, source).size(), 1u);
	EXPECT_EQ(stablesBonuses(*restoredGarrisonHero, source).size(), 1u);
	EXPECT_TRUE(stablesBonuses(*restoredOutsider, source).empty());
	EXPECT_EQ(restoredVisitor->movementPointsLimit(), 240);
	EXPECT_EQ(restoredVisitor->movementPointsRemaining(), 240);

	// At the next day boundary the departed visitor expires to the base pool;
	// current residents get one fresh bonus and the authoritative refill.
	gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 2);
	EXPECT_TRUE(stablesBonuses(*visitor, source).empty());
	EXPECT_EQ(visitor->movementPointsLimit(), 200);
	EXPECT_EQ(visitor->movementPointsRemaining(), 200);
	for(const auto * hero : {garrisonHero, outsider})
	{
		const auto bonuses = stablesBonuses(*hero, source);
		ASSERT_EQ(bonuses.size(), 1u);
		EXPECT_EQ(hero->movementPointsLimit(), 240);
		EXPECT_EQ(hero->movementPointsRemaining(), 240);
	}

	gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 3);
	EXPECT_EQ(stablesBonuses(*garrisonHero, source).size(), 1u);
	EXPECT_EQ(stablesBonuses(*outsider, source).size(), 1u);
	EXPECT_EQ(garrisonHero->movementPointsLimit(), 240);
	EXPECT_EQ(garrisonHero->movementPointsRemaining(), 240);
	EXPECT_EQ(outsider->movementPointsLimit(), 240);
	EXPECT_EQ(outsider->movementPointsRemaining(), 240);
}
