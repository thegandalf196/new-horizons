/*
 * NewHorizonsWaterWalkDayEndTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../mock/TinyMapGameTest.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../SpellPointTestUtils.h"
#include "../battles/FullGameSnapshotTypes.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/pathfinder/CPathfinder.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/pathfinder/TurnInfo.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/processors/TurnOrderProcessor.h"

namespace
{
class NewHorizonsWaterWalkDayEndTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		// This policy fixture isolates the older Water Walk serialization boundary.
		rules.Struct().erase("protectedAdventureBarriers");
		auto & row = rules["adventureSpells"]["core:waterWalk"];
		row.Struct().erase("requireLegalDayEnd");
		if(policy)
			row["requireLegalDayEnd"].Bool() = true;
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}
	void prepare(bool enable = true)
	{
		policy = enable;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(OPPONENT)
			.hero({8, 8, 0}, HeroTypeID(0), PLAYER)
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
			.hero({20, 20, 0}, HeroTypeID(1), PLAYER)
			.hero({30, 30, 0}, HeroTypeID(2), OPPONENT);
		startWithMap(std::move(builder));
		hero = findHeroAt({8, 8, 0});
		other = findHeroAt({20, 20, 0});
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(other, nullptr);
		opponent = findHeroAt({30, 30, 0});
		ASSERT_NE(opponent, nullptr);
		ASSERT_TRUE(hero->usesNewHorizonsMovement());
		revealMap(PLAYER);
		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		handler->turnOrder->addPlayer(PLAYER);
		handler->turnOrder->addPlayer(OPPONENT);
		handler->onNewTurn();
		ASSERT_EQ(gameState()->getCalendar().getCurrentDay(), 1);
		handler->turnOrder->onGameStarted();
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(hero, 100);
		hero->addSpellToSpellbook(SpellID::WATER_WALK);
	}
	std::unique_ptr<BattleInfo> initializedBattle()
	{
		const BattleSideArray<const CGHeroInstance *> heroes = {hero, opponent};
		const BattleSideArray<const CArmedInstance *> armies = {hero, opponent};
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), hero, opponent);
		const std::string battlefieldName = "core:sand_shore";
		const BattleField battlefield(*LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "battlefield", battlefieldName));
		const auto tile = hero->visitablePos();
		return BattleInfo::setupBattle(gameState().get(), tile,
			gameState()->getTile(tile)->getTerrainID(), battlefield, armies, heroes, layout, nullptr);
	}
	void grantWaterWalk(CGHeroInstance * recipient)
	{
		recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_DAY,
			BonusType::WATER_WALKING, BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::WATER_WALK))));
	}
	bool move(CGHeroInstance * actor, const int3 & destination, EPathfindingLayer layer)
	{
		return handler->moveHero(actor->id, actor->convertFromVisitablePos(destination),
			EMovementMode::STANDARD, layer == EPathfindingLayer::WATER, PLAYER, layer);
	}
	void waterSquare(const int3 & center)
	{
		for(int x = -3; x <= 3; ++x)
			for(int y = -3; y <= 3; ++y)
				map()->getTile(center + int3(x, y, 0)).terrainType = ETerrainId::WATER;
	}
	const PlayerColor PLAYER{0};
	const PlayerColor OPPONENT{1};
	bool policy = true;
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * other = nullptr;
	CGHeroInstance * opponent = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
};
}

TEST_F(NewHorizonsWaterWalkDayEndTest, PaidCastExactReserveRejectsBeforeMutationThenLandsAndExpires)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::WATER_WALK);
	AdventureSpellCastParameters cast;
	cast.caster = hero;
	cast.pos = hero->visitablePos();
	const auto mana = hero->getManaAvailable();
	auto * environment = dynamic_cast<SpellCastEnvironment *>(handler->spellcastEnvironment());
	ASSERT_NE(environment, nullptr);
	ASSERT_TRUE(spell.toSpell()->adventureCast(environment, cast));
	EXPECT_EQ(hero->getManaAvailable(), mana - hero->getSpellCost(spell.toSpell()));
	const auto land = hero->visitablePos();
	const auto sea = land + int3(1, 0, 0);
	map()->getTile(sea).terrainType = ETerrainId::WATER;
	hero->setMovementPoints(29);
	EXPECT_FALSE(move(hero, sea, EPathfindingLayer::WATER));
	EXPECT_EQ(hero->visitablePos(), land);
	EXPECT_EQ(hero->movementPointsRemaining(), 29);
	hero->setMovementPoints(30);
	ASSERT_TRUE(move(hero, sea, EPathfindingLayer::WATER));
	EXPECT_EQ(hero->movementPointsRemaining(), 15);
	ASSERT_TRUE(move(hero, land, EPathfindingLayer::LAND));
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	const auto day = gameState()->getCalendar().getCurrentDay();
	ASSERT_EQ(gameState()->getPlayerStatus(PLAYER), EPlayerStatus::INGAME);
	ASSERT_EQ(gameState()->getPlayerStatus(OPPONENT), EPlayerStatus::INGAME);
	ASSERT_TRUE(handler->turnOrder->onPlayerEndsTurn(PLAYER));
	if(gameState()->getCalendar().getCurrentDay() == day)
	{
		ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(OPPONENT));
		ASSERT_TRUE(handler->turnOrder->onPlayerEndsTurn(OPPONENT));
	}
	ASSERT_EQ(gameState()->getCalendar().getCurrentDay(), day + 1);
	PathfinderOptions options(*gameState());
	CPathfinderHelper nextDay(*gameState(), hero, options);
	EXPECT_FALSE(nextDay.getTurnInfo()->hasWaterWalking());
	EXPECT_EQ(hero->visitablePos(), land);
}

TEST_F(NewHorizonsWaterWalkDayEndTest, SharedPlannerRejectsUnsafeWaterNodeAndPreservesExactSafeBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	grantWaterWalk(hero);
	const auto sea = hero->visitablePos() + int3(1, 0, 0);
	map()->getTile(sea).terrainType = ETerrainId::WATER;
	for(const int budget : {29, 30})
	{
		hero->setMovementPoints(budget);
		CPathsInfo paths(gameState()->getMapSize(), hero);
		auto configuration = std::make_shared<SingleHeroPathfinderConfig>(paths, *gameState(), hero);
		configuration->options.turnLimit = 0;
		CPathfinder(*gameState(), configuration).calculatePaths();
		const auto * node = paths.getNode(sea, EPathfindingLayer::WATER);
		EXPECT_EQ(node->reachable(), budget == 30);
		if(budget == 30)
			EXPECT_EQ(node->moveRemains, 15);
	}
}

TEST_F(NewHorizonsWaterWalkDayEndTest, ReserveNeedsExactMultiStepCostNotMerelyAdjacentCoast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto water = hero->visitablePos() + int3(1, 0, 0);
	waterSquare(water);
	const auto shore = water + int3(2, 0, 0);
	map()->getTile(shore).terrainType = ETerrainId::GRASS;
	PathfinderOptions options(*gameState());
	CPathfinderHelper helper(*gameState(), hero, options);
	EXPECT_FALSE(helper.hasSameDayLandEscape(water, 29));
	EXPECT_TRUE(helper.hasSameDayLandEscape(water, 30));
	EXPECT_EQ(hero->visitablePos(), water - int3(1, 0, 0));
}

TEST_F(NewHorizonsWaterWalkDayEndTest, HiddenOccupiedAndVisitableShoreCannotServeAsOrdinaryEscape)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto water = hero->visitablePos() + int3(1, 0, 0);
	waterSquare(water);
	const auto shore = water + int3(1, 0, 0);
	auto & tile = map()->getTile(shore);
	tile.terrainType = ETerrainId::GRASS;
	PathfinderOptions options(*gameState());
	CPathfinderHelper helper(*gameState(), hero, options);
	ASSERT_TRUE(helper.hasSameDayLandEscape(water, 15));
	tile.blockingObjects.push_back(other->id);
	EXPECT_FALSE(helper.hasSameDayLandEscape(water, 15));
	tile.blockingObjects.clear();
	tile.visitableObjects.push_back(other->id);
	EXPECT_FALSE(helper.hasSameDayLandEscape(water, 15));
	tile.visitableObjects.clear();
	setMapVisibility(PLAYER, false);
	EXPECT_FALSE(helper.hasSameDayLandEscape(water, 15));
	setMapVisibility(PLAYER, true);
	EXPECT_TRUE(helper.hasSameDayLandEscape(water, 15));
}

TEST_F(NewHorizonsWaterWalkDayEndTest, ManualEndTurnChecksEveryOwnedHeroAfterAnotherHeroMovedOnLand)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	grantWaterWalk(other);
	const auto shore = other->visitablePos();
	const auto sea = shore + int3(1, 0, 0);
	map()->getTile(sea).terrainType = ETerrainId::WATER;
	other->setMovementPoints(30);
	ASSERT_TRUE(move(other, sea, EPathfindingLayer::WATER));
	ASSERT_TRUE(move(hero, hero->visitablePos() + int3(1, 0, 0), EPathfindingLayer::LAND));
	const auto day = gameState()->getCalendar().getCurrentDay();
	EXPECT_FALSE(handler->turnOrder->onPlayerEndsTurn(PLAYER));
	EXPECT_EQ(gameState()->getCalendar().getCurrentDay(), day);
	EXPECT_EQ(other->visitablePos(), sea);
	EXPECT_EQ(other->movementPointsRemaining(), 15);
	ASSERT_TRUE(move(other, shore, EPathfindingLayer::LAND));
	EXPECT_TRUE(handler->turnOrder->onPlayerEndsTurn(PLAYER));
}

TEST_F(NewHorizonsWaterWalkDayEndTest, OrdinaryEmbarkAndBoatDayEndingRemainLegal)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto sea = hero->visitablePos() + int3(1, 0, 0);
	map()->getTile(sea).terrainType = ETerrainId::WATER;
	handler->createBoat(sea, BoatId::CASTLE, PLAYER);
	hero->setMovementPoints(200);
	ASSERT_TRUE(move(hero, sea, EPathfindingLayer::SAIL));
	ASSERT_TRUE(hero->inBoat());
	EXPECT_TRUE(isWaterWalkDayEndLegal(*gameState(), *hero));
	EXPECT_TRUE(handler->turnOrder->onPlayerEndsTurn(PLAYER));
}

TEST_F(NewHorizonsWaterWalkDayEndTest, HistoricalAbsentPolicyDoesNotTrapAnExistingStrandedHero)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	grantWaterWalk(hero);
	const auto sea = hero->visitablePos() + int3(1, 0, 0);
	map()->getTile(sea).terrainType = ETerrainId::WATER;
	hero->setMovementPoints(15);
	ASSERT_TRUE(move(hero, sea, EPathfindingLayer::WATER));
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	EXPECT_TRUE(handler->turnOrder->onPlayerEndsTurn(PLAYER));
	EXPECT_EQ(hero->visitablePos(), sea);
}

TEST_F(NewHorizonsWaterWalkDayEndTest, KeyPresenceGuardsCurrentAndOlderRawFormatsBeforeWriterPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto previous = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WATER_WALK_DAY_END) - 1);
	const auto rejectsBeforePrefix = [previous](const auto & value)
	{
		CMemorySerializer memory;
		memory.oser.version = previous;
		EXPECT_THROW(memory.oser & value, std::runtime_error);
		EXPECT_TRUE(memory.extractBuffer().empty());
	};
	rejectsBeforePrefix(*gameState());
	rejectsBeforePrefix(*map());
	rejectsBeforePrefix(*hero);
	auto battle = initializedBattle();
	ASSERT_NE(battle, nullptr);
	ASSERT_EQ(battle->getMagicRules(), hero->getMagicRules());
	rejectsBeforePrefix(*battle);
	BattleStart starting;
	starting.battleID = BattleID(0);
	starting.info = initializedBattle();
	ASSERT_TRUE(newHorizonsMagic::requiresWaterWalkLegalDayEnd(starting.info->getMagicRules()));
	rejectsBeforePrefix(starting);
	CMemorySerializer currentStart;
	ASSERT_NO_THROW(currentStart.oser & starting);
	currentStart.iser.cb = gameState().get();
	BattleStart restoredStart;
	ASSERT_NO_THROW(currentStart.iser & restoredStart);
	ASSERT_NE(restoredStart.info, nullptr);
	EXPECT_EQ(restoredStart.battleID, starting.battleID);
	EXPECT_TRUE(newHorizonsMagic::requiresWaterWalkLegalDayEnd(restoredStart.info->getMagicRules()));
	BattleStart plain;
	plain.battleID = BattleID(0);
	CMemorySerializer plainBytes;
	plainBytes.oser.version = previous;
	plainBytes.iser.version = previous;
	ASSERT_NO_THROW(plainBytes.oser & plain);
	BattleStart restoredPlain;
	ASSERT_NO_THROW(plainBytes.iser & restoredPlain);
	EXPECT_EQ(restoredPlain.info, nullptr);
	EXPECT_EQ(restoredPlain.battleID, plain.battleID);
	// No map raw override exists, but the actual serialized hero delegates to
	// its captured world policy. Preflight must precede the map header too.
	CMap delegatedMap(gameState().get());
	const auto & rawSettings = dynamic_cast<const GameSettings &>(delegatedMap.getSettings());
	ASSERT_FALSE(rawSettings.getMagicOverride().has_value());
	const JsonNode originalMagic = rawSettings.getValue(EGameSettings::MAGIC_NEW_HORIZONS);
	CMemorySerializer rawSettingsBytes;
	ASSERT_NO_THROW(rawSettingsBytes.oser & rawSettings);
	JsonNode rawOverrides;
	ASSERT_NO_THROW(rawSettingsBytes.iser & rawOverrides);
	// Writer preflights inspect a mutable local override copy and materialize
	// this null branch; they must not invent a captured magic policy.
	ASSERT_TRUE(rawOverrides["magic"].Struct().contains("newHorizons"));
	ASSERT_TRUE(rawOverrides["magic"]["newHorizons"].isNull());
	EXPECT_FALSE(rawSettings.getMagicOverride().has_value());
	EXPECT_EQ(rawSettings.getValue(EGameSettings::MAGIC_NEW_HORIZONS), originalMagic);
	auto sharedHero = std::dynamic_pointer_cast<CGHeroInstance>(map()->objects.at(hero->id.getNum()));
	ASSERT_NE(sharedHero, nullptr);
	delegatedMap.objects.push_back(sharedHero);
	rejectsBeforePrefix(delegatedMap);
	delegatedMap.objects.clear();
	delegatedMap.addToHeroPool(sharedHero);
	rejectsBeforePrefix(delegatedMap);
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	rejectsBeforePrefix(lobby);
	for(bool enabled : {false, true})
	{
		auto rules = hero->getMagicRules();
		rules["adventureSpells"]["core:waterWalk"]["requireLegalDayEnd"].Bool() = enabled;
		GameSettings settings;
		JsonNode settingsOverrides;
		settingsOverrides["magic"]["newHorizons"] = rules;
		settings.loadOverrides(settingsOverrides);
		rejectsBeforePrefix(settings);
		EXPECT_EQ(newHorizonsMagic::requiresWaterWalkLegalDayEnd(rules), enabled);
		CMemorySerializer current;
		ASSERT_NO_THROW(current.oser & settings);
		GameSettings restoredCurrent;
		ASSERT_NO_THROW(current.iser & restoredCurrent);
		EXPECT_EQ(newHorizonsMagic::requiresWaterWalkLegalDayEnd(
			restoredCurrent.getValue(EGameSettings::MAGIC_NEW_HORIZONS)), enabled);
		CMemorySerializer raw;
		JsonNode overrides;
		overrides["magic"]["newHorizons"] = rules;
		raw.oser & overrides;
		raw.iser.version = previous;
		GameSettings restored;
		EXPECT_THROW(raw.iser & restored, std::runtime_error);
	}
	auto absent = hero->getMagicRules();
	absent["adventureSpells"]["core:waterWalk"].Struct().erase("requireLegalDayEnd");
	EXPECT_NO_THROW(newHorizonsMagic::validateWaterWalkDayEndSerialization(absent, false));
	EXPECT_FALSE(newHorizonsMagic::requiresWaterWalkLegalDayEnd(absent));
	GameSettings historical;
	JsonNode historicalOverrides;
	historicalOverrides["magic"]["newHorizons"] = absent;
	historical.loadOverrides(historicalOverrides);
	CMemorySerializer historicalBytes;
	historicalBytes.oser.version = previous;
	historicalBytes.iser.version = previous;
	ASSERT_NO_THROW(historicalBytes.oser & historical);
	GameSettings restoredHistorical;
	ASSERT_NO_THROW(historicalBytes.iser & restoredHistorical);
	EXPECT_FALSE(newHorizonsMagic::requiresWaterWalkLegalDayEnd(
		restoredHistorical.getValue(EGameSettings::MAGIC_NEW_HORIZONS)));
	absent["adventureSpells"]["core:waterWalk"]["requireLegalDayEnd"] = JsonNode();
	EXPECT_THROW(newHorizonsMagic::validateWaterWalkDayEndSerialization(absent, true), std::runtime_error);
}
