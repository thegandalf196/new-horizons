/*
 * MapScriptTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "LegacyMapImportFixtures.h"

#include "../mock/TinyH3MBuilder.h"
#include "../mock/mock_MapServiceTinyH3M.h"
#include "../mock/mock_Services.h"
#include "../mock/mock_IGameEventCallback.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/StartInfo.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGPandoraBox.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/Quest.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/scripting/NativeMapEventDispatcher.h"
#include "../../lib/scripting/nativeMap/LegacyHotaImport.h"
#include "../../lib/scripting/nativeMap/LegacyHotaPrelude.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/networkPacks/SetScriptVariable.h"

#include <vcmi/ServerCallback.h>
#include <vcmi/scripting/MapEventDispatcher.h>
#include <vcmi/scripting/Service.h>

namespace
{
// Map programs read mutations again after callbacks, including across suspension.
// Apply the same packets as CGameHandler instead of the shared capture-only stubs.
class MapScriptEventCallback final : public GameEventCallbackMock
{
public:
	using GameEventCallbackMock::GameEventCallbackMock;

	void bindState(CGameState & value)
	{
		state = &value;
		setGameInfoCallback(&value);
	}

	void setScriptVariable(const std::string & scope, const std::string & name, const JsonNode & value) override
	{
		SetScriptVariable packet;
		packet.scope = scope;
		packet.name = name;
		packet.value = value;
		sendAndApply(packet);
	}

	void restoreSpellPoints(ObjectInstanceID hid, int32_t amount) override
	{
		GameEventCallbackMock::restoreSpellPoints(hid, amount);
		applyMana(hid, SetMana::Operation::RESTORE_NORMAL, amount);
	}

	void spendSpellPoints(ObjectInstanceID hid, int64_t amount) override
	{
		GameEventCallbackMock::spendSpellPoints(hid, amount);
		applyMana(hid, SetMana::Operation::SPEND, amount);
	}

private:
	void applyMana(ObjectInstanceID hid, SetMana::Operation operation, int64_t amount)
	{
		SetMana packet(hid, operation, amount);
		sendAndApply(packet);
		ASSERT_NE(state, nullptr);
		const auto * hero = state->getHero(hid);
		ASSERT_NE(hero, nullptr);
		manaPointsSet.emplace_back(hid, hero->getManaAvailable());
	}

	CGameState * state = nullptr;
};

const PlayerColor PLAYER = PlayerColor(0);

// Native data fixtures deliberately use the same commands emitted by the HotA converter.
// The fixture resolves @pandora / @town / @monster before game initialization.
constexpr auto ATTACH_SCRIPT = R"json({"format":"new-horizons-native-events","version":1,
 "bindings":[{"object":"@pandora","handler":"onVisit"}],
 "handlers":{"onVisit":{"bucket":"heroEvents","actions":[]}}})json";
constexpr auto BAD_NAME_SCRIPT = R"json({"format":"new-horizons-native-events","version":1,
 "bindings":[{"object":"doesNotExist","handler":"onVisit"}],"handlers":{}})json";
constexpr auto QUESTION_SCRIPT = R"json({"format":"new-horizons-native-events","version":1,"handlers":{
 "onVisit":{"bucket":"heroEvents","actions":[
  {"op":12,"text":"question","images":[],
   "yes":[{"op":4,"amounts":[0,0,0,0,0,0,100]}],
   "no":[{"op":4,"amounts":[0,0,0,0,0,0,5]}]},
  {"op":4,"amounts":[0,0,0,0,0,1,0]}]}}})json";
constexpr auto COMBAT_SCRIPT = R"json({"format":"new-horizons-native-events","version":1,"handlers":{
 "onVisit":{"bucket":"heroEvents","actions":[
  {"op":23,"army":[{"amount":10,"entity":"core:pikeman"}]},
  {"op":4,"amounts":[0,0,0,0,0,0,777]}]}}})json";
constexpr auto SEER_HUT_QUEST_SCRIPT = R"json({"format":"new-horizons-native-events","version":1,"handlers":{
 "questEvents_7":{"bucket":"questEvents","actions":[
  {"op":7,"condition":{"op":0,"value":false},"proposal":"proposal","progression":"progression",
   "completion":"completion","hint":"hint","body":[]}]}}})json";
constexpr auto SEER_HUT_FINISH_SCRIPT = R"json({"format":"new-horizons-native-events","version":1,"handlers":{
 "questEvents_7":{"bucket":"questEvents","actions":[{"op":5,"quest":true}]}}})json";
constexpr auto PREDICATE_SCRIPT = R"json({"format":"new-horizons-native-events","version":1,
 "objects":{"1":"@town","2":"@monster"},"handlers":{
 "checkOwnsTown":{"bucket":"heroEvents","actions":[
  {"op":13,"condition":{"op":18,"player":-1,"object":"1"},
   "yes":[{"op":4,"amounts":[0,0,0,0,0,0,100]}],"no":[{"op":4,"amounts":[0,0,0,0,0,0,5]}]}]},
 "checkDefeatedMonster":{"bucket":"heroEvents","actions":[
  {"op":13,"condition":{"op":14,"player":-1,"object":"2"},
   "yes":[{"op":4,"amounts":[0,0,0,0,0,0,100]}],"no":[{"op":4,"amounts":[0,0,0,0,0,0,5]}]}]}}})json";
constexpr auto GRANT_POINTS_SCRIPT = R"json({"format":"new-horizons-native-events","version":1,"handlers":{
 "grantPoints":{"bucket":"heroEvents","actions":[
  {"op":17,"amount":5,"mode":0},{"op":17,"amount":3,"mode":2},{"op":18,"amount":100,"mode":0}]}}})json";
constexpr auto HIRE_SCRIPT = R"json({"format":"new-horizons-native-events","version":1,"handlers":{
 "hire":{"bucket":"townEvents","actions":[{"op":14,"level":0,"amount":7}]}}})json";
}
class MapScriptTest : public ::testing::Test, public ServerCallback, public MapListener
{
public:
	MapScriptTest()
		: gameEventCallback(std::make_shared<MapScriptEventCallback>(this))
	{
	}

	void SetUp() override
	{
		gameState = std::make_shared<CGameState>();
		gameState->preInit(&services);
	}

	void TearDown() override
	{
		gameState.reset();
		mapService.reset();
		map = nullptr;
	}

	// ---- ServerCallback overrides ----
	bool describeChanges() const override { return true; }
	void apply(CPackForClient & pack) override { gameState->apply(pack); }
	void complain(const std::string & problem) override { FAIL() << "Server-side assertion: " << problem; }
	vstd::RNG * getRNG() override { return &randomGenerator; }
	bool rollCombatAbility(const IBattleInfoCallback &, const battle::Unit &, int) override { return false; }
	void apply(BattleLogMessage &) override {}
	void apply(BattleStackMoved &) override {}
	void apply(BattleUnitsChanged &) override {}
	void apply(SetStackEffect &) override {}
	void apply(StacksInjured &) override {}
	void apply(BattleObstaclesChanged &) override {}
	void apply(CatapultAttack &) override {}

	void mapLoaded(CMap * loadedMap) override
	{
		EXPECT_EQ(map, nullptr);
		map = loadedMap;
		if(map->scriptSource.empty() || map->scriptSource.front() != '{') return;
		JsonNode program(map->scriptSource.data(), map->scriptSource.size(), "native test event fixture");
		const auto resolve = [this](std::string & name)
		{
			const CGObjectInstance * object = name == "@pandora" ? static_cast<const CGObjectInstance *>(findPandora())
				: name == "@town" ? static_cast<const CGObjectInstance *>(findTown())
				: name == "@monster" ? findMonster() : nullptr;
			if(object) name = object->instanceName;
		};
		for(auto & binding : program["bindings"].Vector()) resolve(binding["object"].String());
		for(auto & entry : program["objects"].Struct()) resolve(entry.second.String());
		map->scriptSource = program.toCompactString();
	}

	/// Minimal playable map every test starts from: one active player with a hero, plus the script.
	/// Tests add the object under test on top.
	static TinyH3M::TinyH3MBuilder baseMap(EMapFormat format, const std::string & script)
	{
		TinyH3M::TinyH3MBuilder builder(format);
		builder
			.size(36, false)
			.name("MapScriptTest")
			.playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER)
			.withScript(script);
		return builder;
	}

	void startWithMap(TinyH3M::TinyH3MBuilder builder)
	{
		std::string script = builder.script();
		auto bytes = builder.build();
		mapService = std::make_unique<MapServiceTinyH3M>(std::move(bytes), this, std::move(script));

		StartInfo si;
		si.mapname = "tiny";
		si.difficulty = 0;
		si.mode = EStartMode::NEW_GAME;

		auto header = mapService->loadMapHeader(ResourcePath(si.mapname));
		ASSERT_NE(header.get(), nullptr) << "TinyH3M scenario header failed to load";

		for(int i = 0; i < static_cast<int>(header->players.size()); ++i)
		{
			const PlayerInfo & pinfo = header->players[i];
			if(!(pinfo.canHumanPlay || pinfo.canComputerPlay))
				continue;

			PlayerSettings & pset = si.playerInfos[PlayerColor(i)];
			pset.color = PlayerColor(i);
			pset.connectedPlayerIDs.insert(static_cast<PlayerConnectionID>(i));
			pset.name = "Player";
			pset.castle = pinfo.defaultCastle();
			pset.hero = pinfo.defaultHero();
		}

		GameRandomizer randomizer(*gameState);
		Load::ProgressAccumulator progressTracker;
		gameState->init(mapService.get(), &si, randomizer, progressTracker, false);

		ASSERT_NE(map, nullptr) << "gameState init did not populate the CMap";
		gameEventCallback->bindState(*gameState);
	}

	CGPandoraBox * findPandora() const
	{
		for(const auto & obj : map->objects)
			if(auto * pandora = dynamic_cast<CGPandoraBox *>(obj.get()))
				return pandora;
		return nullptr;
	}

	SeerHut * findSeerHut() const
	{
		for(const auto & obj : map->objects)
			if(auto * hut = dynamic_cast<SeerHut *>(obj.get()))
				return hut;
		return nullptr;
	}

	CGHeroInstance * findHero() const
	{
		for(const auto & obj : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(obj.get()))
				return hero;
		return nullptr;
	}

	CGTownInstance * findTown() const
	{
		for(const auto & obj : map->objects)
			if(auto * town = dynamic_cast<CGTownInstance *>(obj.get()))
				return town;
		return nullptr;
	}

	CGObjectInstance * findMonster() const
	{
		for(const auto & obj : map->objects)
			if(auto * monster = dynamic_cast<CGCreature *>(obj.get()))
				return monster;
		return nullptr;
	}

	int gold() const { return gameState->players.at(PLAYER).resources[GameResID::GOLD]; }
	int gems() const { return gameState->players.at(PLAYER).resources[GameResID::GEMS]; }

protected:
	std::shared_ptr<CGameState>            gameState;
	std::shared_ptr<MapScriptEventCallback> gameEventCallback;
	std::unique_ptr<MapServiceTinyH3M>     mapService;
	ServicesMock                           services;
	CMap *                                 map = nullptr;
	CRandomGenerator                       randomGenerator;
};

TEST_F(MapScriptTest, initBindsHandlerToPandoraByInstanceName)
{
	auto builder = baseMap(EMapFormat::SOD, ATTACH_SCRIPT);
	builder.pandora({10, 10, 0});

	startWithMap(builder);

	CGPandoraBox * pandora = findPandora();
	ASSERT_NE(pandora, nullptr) << "the built map has no Pandora's Box";
	EXPECT_EQ(pandora->heroVisitScriptHandler, "onVisit")
		<< "init should have bound onVisit to the pandora via attachEventScript";
}

TEST_F(MapScriptTest, attachToUnknownObjectRejectsMap)
{
 auto builder = baseMap(EMapFormat::SOD, BAD_NAME_SCRIPT);
 builder.pandora({10, 10, 0});
 EXPECT_THROW(startWithMap(builder), std::runtime_error);
}
TEST_F(MapScriptTest, showQuestionPausesUntilAnswered)
{
	auto builder = baseMap(EMapFormat::SOD, QUESTION_SCRIPT);
	builder.pandora({10, 10, 0});

	startWithMap(builder);

	auto * dispatcher = gameState->getMapEventDispatcher();
	ASSERT_NE(dispatcher, nullptr);
	CGPandoraBox * pandora = findPandora();
	CGHeroInstance * hero = findHero();
	ASSERT_NE(pandora, nullptr);
	ASSERT_NE(hero, nullptr);

	const int baseGold = gold();
	const int baseGems = gems();

	// Firing the handler must pause on the question: a dialog is shown and nothing after it has run.
	auto handle = dispatcher->onObjectVisit(*gameEventCallback, "onVisit", pandora, hero);
	ASSERT_TRUE(handle.has_value()) << "the handler must pause on showQuestion";
	EXPECT_EQ(gameEventCallback->blockingDialogs.size(), 1u) << "the question dialog must be shown";
	EXPECT_EQ(gold(), baseGold) << "no branch may run before the answer";
	EXPECT_EQ(gems(), baseGems) << "the sibling after showQuestion must not run before the answer";

	// Answering yes runs the onYes branch and then the sibling statement, in that order.
	bool finished = dispatcher->resumeCoroutine(*gameEventCallback, *handle, 1);
	EXPECT_TRUE(finished) << "the coroutine finishes once the reward is granted";
	EXPECT_EQ(gold(), baseGold + 100) << "onYes must grant 100 gold";
	EXPECT_EQ(gems(), baseGems + 1) << "the sibling after showQuestion must run after the answer";
}

TEST_F(MapScriptTest, startCombatReplacesGarrisonAndPauses)
{
	auto builder = baseMap(EMapFormat::SOD, COMBAT_SCRIPT);
	builder.pandora({10, 10, 0});

	startWithMap(builder);

	auto * dispatcher = gameState->getMapEventDispatcher();
	CGPandoraBox * pandora = findPandora();
	CGHeroInstance * hero = findHero();
	ASSERT_NE(dispatcher, nullptr);
	ASSERT_NE(pandora, nullptr);
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(pandora->stacksCount(), 0) << "the built pandora starts with no army";

	const int baseGold = gold();

	auto handle = dispatcher->onObjectVisit(*gameEventCallback, "onVisit", pandora, hero);
	ASSERT_TRUE(handle.has_value()) << "startCombat must pause the script";

	// The event army replaced the (empty) garrison, and the post-combat grant has not run yet.
	ASSERT_EQ(pandora->stacksCount(), 1) << "the event army must be placed into the pandora";
	ASSERT_NE(pandora->getCreature(SlotID(0)), nullptr);
	EXPECT_EQ(pandora->getCreature(SlotID(0))->getJsonKey(), "core:pikeman") << "the event creature must be placed";
	EXPECT_EQ(pandora->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(gold(), baseGold) << "the post-combat grant must wait for the battle to resolve";
}

TEST_F(MapScriptTest, seerHutScriptedQuestTracksProposalThenProgression)
{
	auto builder = baseMap(EMapFormat::HOTA, SEER_HUT_QUEST_SCRIPT);
	builder.seerHut({10, 10, 0}, TinyH3M::TinyH3MBuilder::missionScripted(7));

	startWithMap(builder);

	auto * dispatcher = gameState->getMapEventDispatcher();
	SeerHut * hut = findSeerHut();
	CGHeroInstance * hero = findHero();
	ASSERT_NE(dispatcher, nullptr);
	ASSERT_NE(hut, nullptr);
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(hut->getActiveQuest(), nullptr) << "the built quest must classify as HOTA_SCRIPTED";
	EXPECT_EQ(hut->getActiveQuest()->missionKind, EQuestMission::HOTA_SCRIPTED);
	EXPECT_FALSE(hut->getActiveQuest()->isKnownTo(PLAYER));

	// First visit: not yet proposed -> proposal branch, marks the quest known and logs it.
	auto handle = dispatcher->onObjectVisit(*gameEventCallback, "questEvents_7", hut, hero);
	EXPECT_FALSE(handle.has_value()) << "this handler never blocks";
	EXPECT_TRUE(hut->getActiveQuest()->isKnownTo(PLAYER)) << "markQuestProposed must record the visit";
	EXPECT_EQ(gameState->players.at(PLAYER).quests.size(), 1u) << "addToQuestLog must add exactly one entry";
	ASSERT_EQ(gameEventCallback->infoWindows.size(), 1u);

	// Repeat visit: already proposed -> progression branch, no duplicate log entry.
	dispatcher->onObjectVisit(*gameEventCallback, "questEvents_7", hut, hero);
	EXPECT_EQ(gameState->players.at(PLAYER).quests.size(), 1u) << "a repeat visit must not add a duplicate quest log entry";
	ASSERT_EQ(gameEventCallback->infoWindows.size(), 2u);
}

TEST_F(MapScriptTest, finishQuestOrRemoveObjectEmptiesSeerHutWithoutRemovingIt)
{
	auto builder = baseMap(EMapFormat::HOTA, SEER_HUT_FINISH_SCRIPT);
	builder.seerHut({10, 10, 0}, TinyH3M::TinyH3MBuilder::missionScripted(7));

	startWithMap(builder);

	auto * dispatcher = gameState->getMapEventDispatcher();
	SeerHut * hut = findSeerHut();
	CGHeroInstance * hero = findHero();
	ASSERT_NE(dispatcher, nullptr);
	ASSERT_NE(hut, nullptr);
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(hut->getActiveQuest(), nullptr);

	dispatcher->onObjectVisit(*gameEventCallback, "questEvents_7", hut, hero);

	EXPECT_EQ(hut->getActiveQuest(), nullptr) << "finishQuestOrRemoveObject must empty a seer hut's quest, not remove the hut";
	EXPECT_EQ(findSeerHut(), hut) << "the seer hut object itself must remain on the map";
}

TEST_F(MapScriptTest, playerOwnsTownResolvesObjectByName)
{
	auto builder = baseMap(EMapFormat::SOD, PREDICATE_SCRIPT);
	builder.randomTown({10, 10, 0}, PLAYER);

	startWithMap(builder);

	auto * dispatcher = gameState->getMapEventDispatcher();
	CGTownInstance * town = findTown();
	CGHeroInstance * hero = findHero();
	ASSERT_NE(dispatcher, nullptr);
	ASSERT_NE(town, nullptr) << "the built map has no town";
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(town->getOwner(), PLAYER);

	const int baseGold = gold();
	dispatcher->onObjectVisit(*gameEventCallback, "checkOwnsTown", town, hero);
	EXPECT_EQ(gold(), baseGold + 100) << "playerOwnsTown must resolve the town by name and see PLAYER owns it";
}

TEST_F(MapScriptTest, playerDefeatedMonsterReadsDestroyedObjects)
{
	auto builder = baseMap(EMapFormat::SOD, PREDICATE_SCRIPT);
	builder.monster({10, 10, 0}, CreatureID(0), 10);

	startWithMap(builder);

	auto * dispatcher = gameState->getMapEventDispatcher();
	CGObjectInstance * monster = findMonster();
	CGHeroInstance * hero = findHero();
	ASSERT_NE(dispatcher, nullptr);
	ASSERT_NE(monster, nullptr) << "the built map has no monster";
	ASSERT_NE(hero, nullptr);

	const int baseGold = gold();

	// Not defeated yet -> false branch grants 5 gold.
	dispatcher->onObjectVisit(*gameEventCallback, "checkDefeatedMonster", monster, hero);
	EXPECT_EQ(gold(), baseGold + 5) << "an undefeated monster must read as not-defeated";

	// Record the defeat and fire again -> true branch grants 100 gold.
	gameState->players.at(PLAYER).destroyedObjects.insert(monster->id);
	dispatcher->onObjectVisit(*gameEventCallback, "checkDefeatedMonster", monster, hero);
	EXPECT_EQ(gold(), baseGold + 5 + 100) << "a monster in the player's destroyedObjects must read as defeated";
}

TEST_F(MapScriptTest, grantPointsRespectMode)
{
	auto builder = baseMap(EMapFormat::SOD, GRANT_POINTS_SCRIPT);

	startWithMap(builder);

	auto * dispatcher = gameState->getMapEventDispatcher();
	CGHeroInstance * hero = findHero();
	ASSERT_NE(dispatcher, nullptr);
	ASSERT_NE(hero, nullptr);

	// Leave room for the +5 restoration under the current Normal mana cap.
	ASSERT_GE(hero->manaLimit(), 5);
	SetMana emptyMana(hero->id, SetMana::Operation::RESTORE_SNAPSHOT, 0, 0);
	apply(emptyMana);
	ASSERT_EQ(hero->getManaAvailable(), 0);
	const int baseMana = hero->getManaAvailable();
	const int baseMove = hero->movementPointsRemaining();

	dispatcher->onObjectVisit(*gameEventCallback, "grantPoints", hero, hero);

	ASSERT_EQ(gameEventCallback->manaPointsSet.size(), 2u);
	EXPECT_EQ(gameEventCallback->manaPointsSet[0].second, baseMana + 5) << "mode 0 adds to the current mana";
	EXPECT_EQ(gameEventCallback->manaPointsSet[1].second, 3)            << "mode 2 sets the total directly";
	ASSERT_EQ(gameEventCallback->movePointsSet.size(), 1u);
	EXPECT_EQ(gameEventCallback->movePointsSet[0].second, baseMove + 100) << "mode 0 adds to the current movement";
}

TEST_F(MapScriptTest, grantCreaturesToHireChangesTownPool)
{
	auto builder = baseMap(EMapFormat::SOD, HIRE_SCRIPT);
	builder.randomTown({10, 10, 0}, PLAYER);

	startWithMap(builder);

	auto * dispatcher = gameState->getMapEventDispatcher();
	CGTownInstance * town = findTown();
	ASSERT_NE(dispatcher, nullptr);
	ASSERT_NE(town, nullptr);
	ASSERT_FALSE(town->creatures.empty());

	const int base = town->creatures[0].first;

	dispatcher->onTownTurnStart(*gameEventCallback, "hire", town);

	EXPECT_EQ(town->creatures[0].first, base + 7) << "grantCreaturesToHire adds to the town's tier-0 pool";
}

TEST_F(MapScriptTest, loopKeepsPrivateCounterAcrossQuestionsAndRewardAcknowledgement)
{
	const std::string program = R"json({"format":"new-horizons-native-events","version":1,
 "variables":{"1":"loop"},"handlers":{"onVisit":{"bucket":"heroEvents","actions":[
  {"op":28,"variable":1,"from":1,"to":2,"body":[
   {"op":12,"text":"question","yes":[
    {"op":4,"amounts":[0,0,0,0,0,0,{"op":1,"variable":1}]},
    {"op":3,"variable":1,"mode":2,"value":99}],"no":[]}]},
  {"op":6,"text":"reward","body":[{"op":4,"amounts":[0,0,0,0,0,0,7]}]},
  {"op":4,"amounts":[0,0,0,0,0,1,0]}]}}})json";
	auto builder = baseMap(EMapFormat::SOD, program);
	builder.pandora({10, 10, 0});
	startWithMap(builder);
	auto * dispatcher = gameState->getMapEventDispatcher();
	ASSERT_NE(dispatcher, nullptr);
	const int initialGold = gold(), initialGems = gems();
	auto handle = dispatcher->onObjectVisit(*gameEventCallback, "onVisit", findPandora(), findHero());
	ASSERT_TRUE(handle.has_value());
	EXPECT_EQ(gold(), initialGold);
	EXPECT_FALSE(dispatcher->resumeCoroutine(*gameEventCallback, *handle, 1));
	EXPECT_EQ(gold(), initialGold + 1);
	EXPECT_FALSE(dispatcher->resumeCoroutine(*gameEventCallback, *handle, 1));
	EXPECT_EQ(gold(), initialGold + 3);
	EXPECT_EQ(gems(), initialGems);
	EXPECT_TRUE(dispatcher->resumeCoroutine(*gameEventCallback, *handle, std::nullopt));
	EXPECT_EQ(gold(), initialGold + 10);
	EXPECT_EQ(gems(), initialGems + 1);
	EXPECT_EQ(gameEventCallback->blockingDialogs.size(), 3u);
	EXPECT_EQ(map->getScriptVariables().get(ModScope::scopeMap(), "loop").Float(), 99);
}

TEST_F(MapScriptTest, conditionShortCircuitPreservesRandomGeneratorState)
{
	const std::string program = R"json({"format":"new-horizons-native-events","version":1,"handlers":{
 "onVisit":{"bucket":"heroEvents","actions":[{"op":13,"condition":{"op":1,"args":[
  {"op":0,"value":false},{"op":5,"left":{"op":16,"left":1,"right":10},"right":1}]},
  "yes":[],"no":[]}]}}})json";
	auto builder = baseMap(EMapFormat::SOD, program);
	startWithMap(builder);
	randomGenerator.setSeed(179);
	CRandomGenerator expected(179);
	auto * dispatcher = gameState->getMapEventDispatcher();
	ASSERT_NE(dispatcher, nullptr);
	EXPECT_FALSE(dispatcher->onObjectVisit(*gameEventCallback, "onVisit", findHero(), findHero()).has_value());
	EXPECT_EQ(randomGenerator.nextInt(), expected.nextInt());
}

TEST_F(MapScriptTest, rebuildingDispatcherPreservesSavedVariablesAndBindings)
{
	auto builder = baseMap(EMapFormat::SOD, ATTACH_SCRIPT);
	builder.pandora({10, 10, 0});
	startWithMap(builder);
	auto * object = findPandora();
	ASSERT_NE(object, nullptr);
	object->heroVisitScriptHandler = "savedBinding";
	map->getScriptVariables().set(ModScope::scopeMap(), "saved", JsonNode(123));
	auto rebuilt = scripting::createNativeMapEventDispatcher(*gameState, false);
	ASSERT_NE(rebuilt, nullptr);
	EXPECT_EQ(object->heroVisitScriptHandler, "savedBinding");
	EXPECT_EQ(map->getScriptVariables().get(ModScope::scopeMap(), "saved").Integer(), 123);
}

namespace
{
std::string generatedLegacyProgram(const std::string & handlers)
{
	std::vector<std::string> order;
	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i) order.push_back(GameResID::encode(i));
	return legacyMapFixtures::generatedProgram(handlers, order);
}
}

TEST_F(MapScriptTest, generatedLegacyMapMigratesAndKeepsQuestionOrdering)
{
	// The fixture library is already loaded before the per-test game is initialized.
	const auto source = generatedLegacyProgram(legacyMapFixtures::QUESTION_HANDLERS);
	auto builder = baseMap(EMapFormat::SOD, source); builder.pandora({10, 10, 0});
	startWithMap(builder);
	ASSERT_EQ(map->scriptSource.front(), '{') << "future saves must store the migrated native program";
	auto * dispatcher = gameState->getMapEventDispatcher(); ASSERT_NE(dispatcher, nullptr);
	const int initialGold = gold(), initialGems = gems();
	auto handle = dispatcher->onObjectVisit(*gameEventCallback, "heroEvents_1", findPandora(), findHero());
	ASSERT_TRUE(handle.has_value()); EXPECT_EQ(gold(), initialGold); EXPECT_EQ(gems(), initialGems);
	EXPECT_TRUE(dispatcher->resumeCoroutine(*gameEventCallback, *handle, 0));
	EXPECT_EQ(gold(), initialGold + 5); EXPECT_EQ(gems(), initialGems + 1);
}

TEST_F(MapScriptTest, legacyImporterRecognizesNestedLoopsArithmeticAndPredicates)
{
	const auto source = generatedLegacyProgram(legacyMapFixtures::NESTED_HANDLERS);
	const auto program = scripting::nativeMap::importLegacyHotaEvents(source);
	const auto & actions = program["handlers"]["heroEvents_1"]["actions"].Vector();
	ASSERT_EQ(actions.size(), 3u);
	EXPECT_EQ(actions[0]["op"].Integer(), 28);
	EXPECT_EQ(actions[0]["to"]["op"].Integer(), 16);
	EXPECT_EQ(actions[0]["variable"].Integer(), 7);
	const auto & branches = actions[0]["body"].Vector().at(0)["branches"].Vector();
	ASSERT_EQ(branches.size(), 3u);
	EXPECT_EQ(branches[1]["body"].Vector().at(0)["amount"].Integer(), -2);
	EXPECT_EQ(actions[1]["body"].Vector().at(0)["scroll"].String(), "core:magicArrow");
	EXPECT_EQ(actions[2]["handler"].String(), "heroEvents_2");
	EXPECT_EQ(program["variables"]["7"].String(), "counter");
}

TEST_F(MapScriptTest, legacyImporterRejectsAuthoredOrModifiedHelpers)
{
	EXPECT_THROW(scripting::nativeMap::importLegacyHotaEvents("local Map = {} return Map"), std::runtime_error);
	auto source = generatedLegacyProgram("");
	const auto start = source.find("if player < 0"); ASSERT_NE(start, std::string::npos);
	source.replace(start, std::string("if player < 0").size(), "if player < 1");
	EXPECT_THROW(scripting::nativeMap::importLegacyHotaEvents(source), std::runtime_error);
}

TEST_F(MapScriptTest, legacyImporterRejectsCallsWhoseIgnoredInputsWouldChangeBehavior)
{
	const auto & rejected = legacyMapFixtures::rejectedStatements();
	for(const auto & statement : rejected)
	{
		SCOPED_TRACE(statement);
		const auto source = generatedLegacyProgram("function Map:heroEvents_1(game, server, object, hero)\n local player = hero:getOwner()\n" + statement + "\nend\n");
		EXPECT_THROW(scripting::nativeMap::importLegacyHotaEvents(source), std::runtime_error);
	}
}

TEST_F(MapScriptTest, legacyImporterAcceptsCompleteGeneratedSignatures)
{
	const auto source = generatedLegacyProgram(legacyMapFixtures::COMPLETE_HANDLERS);
	EXPECT_NO_THROW(scripting::nativeMap::importLegacyHotaEvents(source));
}
