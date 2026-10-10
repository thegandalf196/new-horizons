/*
 * NewHorizonsLighthouseDepartureTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt.
 */
#pragma once
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameSettings.h"
#include "../../lib/callback/Calendar.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/pathfinder/NewHorizonsLighthouse.h"
#include "../../lib/pathfinder/NewHorizonsMovement.h"
#include "../../lib/pathfinder/CPathfinder.h"
#include "../../lib/pathfinder/PathfinderCache.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../lib/pathfinder/TurnInfo.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../../server/CGameHandler.h"

namespace
{
class NewHorizonsLighthouseDepartureTest : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		// Current admission must be shipped, not a fixture-only feature override.
		ASSERT_TRUE(rules["lighthouseDeparture"]["enabled"].Bool());
		ASSERT_EQ(rules["lighthouseDeparture"]["seaMovementPercent"].Integer(), 20);
		if(!feature)
			rules.Struct().erase("lighthouseDeparture");
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void start(bool enabled = true)
	{
		feature = enabled;
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Castle Lighthouse departure").playerActive(PlayerColor(0))
			.town({10, 10, 0}, FactionID::CASTLE, PlayerColor(0))
			.hero({5, 13, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		hero = findFirst<CGHeroInstance>();
		town = findFirst<CGTownInstance>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(town, nullptr);
		server = std::make_unique<GameHandlerTestServer>(gameState(), PlayerColor(0));
		handler = std::make_unique<CGameHandler>(*server, gameState());
		// Departure receipts name a real calendar day, never the pre-game day 0.
		handler->onNewTurn();
		ASSERT_EQ(gameState()->getCalendar().getCurrentDay(), 1);
		std::vector<int3> offsets;
		town->getOutOffsets(offsets);
		ASSERT_FALSE(offsets.empty());
		port = town->visitablePos() + offsets.front();
		shore = port + int3(-1, 0, 0);
		map()->getTile(port).terrainType = ETerrainId::WATER;
		map()->getTile(port + int3(0, 1, 0)).terrainType = ETerrainId::WATER;
		map()->getTile(shore).terrainType = ETerrainId::GRASS;
		ChangeObjPos relocate;
		relocate.objid = hero->id;
		relocate.nPos = shore;
		relocate.initiator = hero->getOwner();
		gameState()->apply(relocate);
		ASSERT_EQ(hero->visitablePos(), shore);
		if(!town->hasBuilt(BuildingID::SHIPYARD))
			ASSERT_TRUE(handler->buildStructure(town->id, BuildingID::SHIPYARD, true));
		if(!town->hasBuilt(BuildingID::SPECIAL_1))
			ASSERT_TRUE(handler->buildStructure(town->id, BuildingID::SPECIAL_1, true));
		handler->createBoat(port, BoatId::CASTLE, hero->getOwner());
		revealMap(hero->getOwner());
		hero->setMovementPoints(hero->getTurnInfo(0)->getMaxMovePoints(EPathfindingLayer::LAND));
	}

	TryMoveHero receipt(int dayOffset = 0)
	{
		const PathfinderOptions options(*gameState());
		CPathfinderHelper helper(*gameState(), hero, options);
		TryMoveHero pack;
		pack.id = hero->id;
		pack.result = TryMoveHero::EMBARK;
		pack.start = hero->anchorPos();
		pack.end = hero->convertFromVisitablePos(port);
		pack.lighthouseDeparture = newHorizonsLighthouse::DepartureReceipt{
			town->id, gameState()->getCalendar().getCurrentDay() + dayOffset};
		const int cost = helper.getMovementCost(shore, port, EPathfindingLayer::SAIL,
			hero->movementPointsRemaining());
		pack.movePoints = newHorizonsLighthouse::movementAfterDeparture(hero->movementPointsRemaining(), cost, *helper.getTurnInfo());
		return pack;
	}

	bool feature = true;
	CGHeroInstance * hero = nullptr;
	CGTownInstance * town = nullptr;
	int3 shore, port;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
};
}
