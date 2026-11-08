/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/mock_IGameEventCallback.h"
#include "../map/LegacyMapImportFixtures.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../tools/native/SaveParityInput.h"
#include "../../tools/native/SaveParityEvidence.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGPandoraBox.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/scripting/nativeMap/LegacyHotaImport.h"
#include "../../lib/networkPacks/SetScriptVariable.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/CLoadFile.h"
#include "../../lib/serializer/CSaveFile.h"
#include "../../server/CGameHandler.h"
#include "../../server/TurnTimerHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/processors/HeroPoolProcessor.h"
#include "../../server/processors/PlayerMessageProcessor.h"
#include "../../server/processors/TurnOrderProcessor.h"
#include <vcmi/scripting/MapEventDispatcher.h>
#include <boost/filesystem/operations.hpp>
#include <fstream>

namespace
{
const PlayerColor PLAYER(0);

template<class T> std::vector<std::byte> bytes(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

struct EventSave
{
	boost::filesystem::path directory = boost::filesystem::temp_directory_path()
		/ boost::filesystem::unique_path("nh-completed-event-%%%%%%%%-%%%%%%%%");
	boost::filesystem::path path = directory / "event.vsgm1";
	EventSave() { boost::filesystem::create_directory(directory); }
	~EventSave() { boost::filesystem::remove_all(directory); }
	std::string contents() const
	{
		std::ifstream input(path.string(), std::ios::binary);
		return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	}
	void write(const CGameState & world, const CGameHandler & handler) const
	{
		CSaveFile file;
		world.saveGame(file);
		file.save(handler);
		file.write(path);
	}
};

std::unique_ptr<saveParity::LoadedSave> ordinaryLoad(const EventSave & save)
{
	auto loaded = std::make_unique<saveParity::LoadedSave>();
	CLoadFile file(save.path, loaded->world.get());
	loaded->world->loadGame(file);
	file.load(*loaded->handler);
	if(!file.atEnd()) throw std::runtime_error("Completed-event save has trailing data");
	return loaded;
}

// The shared map mock already emits canonical resource packets. Bind all used
// mutations to the real loaded world, rather than a capture-only callback.
class EventCallback final : public GameEventCallbackMock
{
	CGameState & world;
	vstd::RNG & rng;
public:
	std::vector<std::vector<std::byte>> packets;
	EventCallback(CGameState & world, vstd::RNG & rng)
		: GameEventCallbackMock(nullptr), world(world), rng(rng)
	{ setGameInfoCallback(&world); }
	void sendAndApply(CPackForClient & packet) override
	{
		packets.push_back(bytes(static_cast<const CPackForClient *>(&packet)));
		world.apply(packet);
	}
	void setScriptVariable(const std::string & scope, const std::string & name, const JsonNode & value) override
	{
		SetScriptVariable packet;
		packet.scope = scope;
		packet.name = name;
		packet.value = value;
		sendAndApply(packet);
	}
	vstd::RNG & getRandomGenerator() override { return rng; }
};

void completeQuestion(CGameState & world, EventCallback & callback, ObjectInstanceID boxId, ObjectInstanceID heroId,
	std::optional<int> loopBound = std::nullopt)
{
	auto * dispatcher = world.getMapEventDispatcher();
	ASSERT_NE(dispatcher, nullptr);
	const auto * box = dynamic_cast<const CGPandoraBox *>(world.getObj(boxId, false));
	const auto * hero = world.getHero(heroId);
	ASSERT_NE(box, nullptr);
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(box->heroVisitScriptHandler, "heroEvents_1");
	const auto before = world.players.at(PLAYER).resources;
	const auto handle = dispatcher->onObjectVisit(callback, box->heroVisitScriptHandler, box, hero);
	ASSERT_TRUE(handle.has_value());
	ASSERT_EQ(callback.blockingDialogs.size(), 1u);
	EXPECT_EQ(world.players.at(PLAYER).resources, before);
	const size_t variablePackets = loopBound ? 2 * *loopBound : 0;
	EXPECT_EQ(callback.packets.size(), variablePackets);
	ASSERT_TRUE(dispatcher->resumeCoroutine(callback, *handle, 0));
	auto expected = before;
	expected[GameResID::GOLD] += 5;
	expected[GameResID::GEMS] += 1;
	EXPECT_EQ(world.players.at(PLAYER).resources, expected);
	EXPECT_EQ(world.getMap().getScriptVariables().get(ModScope::scopeMap(), "counter").Float(), loopBound ? *loopBound - 3 : 123);
	ASSERT_EQ(callback.packets.size(), variablePackets + 2);
}

class CompletedMapEventSaveTest : public TinyMapGameTest
{
	NativeBonusGraphAccess access;
protected:
	saveParity::InspectionServer server;
	std::unique_ptr<CGameHandler> handler;
	ObjectInstanceID boxId, heroId;
	Services * gameServices() override { return LIBRARY; }
	virtual std::string savedProgram(const std::vector<std::string> & resources)
	{ return legacyMapFixtures::generatedProgram(legacyMapFixtures::QUESTION_HANDLERS, resources); }
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PLAYER)
			.pandora({10, 10, 0});
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder)));
		map()->registerMapStrings();
		auto * box = findFirst<CGPandoraBox>();
		auto * hero = findHeroByOwner(PLAYER);
		ASSERT_NE(box, nullptr);
		ASSERT_NE(hero, nullptr);
		boxId = box->id;
		heroId = hero->id;
		box->heroVisitScriptHandler = "heroEvents_1";
		std::vector<std::string> resources;
		for(int index = 0; index < GameConstants::RESOURCE_QUANTITY; ++index)
			resources.push_back(GameResID::encode(index));
		map()->scriptSource = savedProgram(resources);
		gameState()->actingPlayers = {PLAYER};
		gameState()->players.at(PLAYER).human = true;
		handler = std::make_unique<CGameHandler>(server, gameState());
		handler->randomizer->setSeed(42);
		EventCallback callback(*gameState(), handler->getRandomGenerator());
		callback.setScriptVariable(ModScope::scopeMap(), "counter", JsonNode(123));
		gameState()->replayLog.configure(false, 1);
		gameState()->replayLog.beginDay(gameState()->day, gameState()->saveReplaySnapshot());
		gameState()->replayLog.addTurn(PLAYER);
	}
	void TearDown() override
	{
		handler.reset();
		TinyMapGameTest::TearDown();
	}
};

class RandomCompletedMapEventSaveTest : public CompletedMapEventSaveTest
{
protected:
	std::string savedProgram(const std::vector<std::string> & resources) override
	{
		// Reuse the exact imported loop/RNG/variable operations already covered
		// by NativeMapImportTest, followed by the unchanged question actions.
		auto question = scripting::nativeMap::importLegacyHotaProgram(
			legacyMapFixtures::generatedProgram(legacyMapFixtures::QUESTION_HANDLERS, resources), resources);
		const auto nested = scripting::nativeMap::importLegacyHotaProgram(
			legacyMapFixtures::generatedProgram(legacyMapFixtures::NESTED_HANDLERS, resources), resources);
		auto & actions = question["handlers"]["heroEvents_1"]["actions"].Vector();
		actions.insert(actions.begin(), nested["handlers"]["heroEvents_1"]["actions"].Vector().at(0));
		return question.toCompactString();
	}
};

TEST_F(CompletedMapEventSaveTest, CompletedQuestionDiskRoundTripPreservesStateRngAndNextHandler)
{
	const auto defaultRng = bytes(CRandomGenerator::getDefault());
	EventSave beforeEvent;
	beforeEvent.write(*gameState(), *handler); // No event has begun.
	const auto original = beforeEvent.contents();
	auto uninterrupted = ordinaryLoad(beforeEvent);
	ASSERT_NE(uninterrupted->world->getMapEventDispatcher(), nullptr);
	ASSERT_FALSE(uninterrupted->world->getMap().scriptSource.empty());
	EXPECT_EQ(uninterrupted->world->getMap().scriptSource.front(), '{');
	EventCallback first(*uninterrupted->world, uninterrupted->handler->getRandomGenerator());
	const auto rngBefore = bytes(*uninterrupted->handler->randomizer);
	ASSERT_NO_FATAL_FAILURE(completeQuestion(*uninterrupted->world, first, boxId, heroId));
	EXPECT_EQ(bytes(*uninterrupted->handler->randomizer), rngBefore);
	const auto completed = saveParity::capture(*uninterrupted->handler);
	EventSave afterEvent;
	afterEvent.write(*uninterrupted->world, *uninterrupted->handler); // resume returned finished.
	const auto completedBytes = afterEvent.contents();
	auto reloaded = ordinaryLoad(afterEvent);
	ASSERT_NE(reloaded->world->getMapEventDispatcher(), nullptr);
	const auto restored = saveParity::capture(*reloaded->handler);
	EXPECT_EQ(saveParity::firstDifference(completed, restored), "");
	EXPECT_EQ(restored.rawStatistics, completed.rawStatistics);
	EXPECT_EQ(reloaded->world->getMap().scriptSource, uninterrupted->world->getMap().scriptSource);
	const auto * savedBox = dynamic_cast<const CGPandoraBox *>(reloaded->world->getObj(boxId, false));
	ASSERT_NE(savedBox, nullptr);
	EXPECT_EQ(savedBox->heroVisitScriptHandler, "heroEvents_1");
	EXPECT_EQ(reloaded->world->getMap().getScriptVariables().get(ModScope::scopeMap(), "counter").Integer(), 123);
	EventCallback nextOriginal(*uninterrupted->world, uninterrupted->handler->getRandomGenerator());
	EventCallback nextLoaded(*reloaded->world, reloaded->handler->getRandomGenerator());
	ASSERT_NO_FATAL_FAILURE(completeQuestion(*uninterrupted->world, nextOriginal, boxId, heroId));
	ASSERT_NO_FATAL_FAILURE(completeQuestion(*reloaded->world, nextLoaded, boxId, heroId));
	EXPECT_EQ(nextLoaded.packets, nextOriginal.packets);
	EXPECT_EQ(nextLoaded.packets, first.packets);
	EXPECT_EQ(saveParity::firstDifference(saveParity::capture(*uninterrupted->handler),
		saveParity::capture(*reloaded->handler)), "");
	EXPECT_EQ(bytes(*uninterrupted->handler->randomizer), rngBefore);
	EXPECT_EQ(bytes(*reloaded->handler->randomizer), rngBefore);
	EXPECT_EQ(bytes(CRandomGenerator::getDefault()), defaultRng);
	EXPECT_EQ(beforeEvent.contents(), original);
	EXPECT_EQ(afterEvent.contents(), completedBytes);
}

TEST_F(RandomCompletedMapEventSaveTest, RandomVariableLoopDiskRoundTripContinuesNextDrawAndHandler)
{
	const auto defaultRng = bytes(CRandomGenerator::getDefault());
	EventSave beforeEvent;
	beforeEvent.write(*gameState(), *handler);
	const auto original = beforeEvent.contents();
	auto uninterrupted = ordinaryLoad(beforeEvent);
	const auto rngBefore = bytes(*uninterrupted->handler->randomizer);
	CRandomGenerator expected(42); // The real server RNG was seeded in SetUp.
	const int firstBound = expected.nextInt(2, 3);
	EventCallback first(*uninterrupted->world, uninterrupted->handler->getRandomGenerator());
	ASSERT_NO_FATAL_FAILURE(completeQuestion(*uninterrupted->world, first, boxId, heroId, firstBound));
	EXPECT_NE(bytes(*uninterrupted->handler->randomizer), rngBefore);
	EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(uninterrupted->handler->getRandomGenerator())), bytes(expected));
	const auto completed = saveParity::capture(*uninterrupted->handler);
	EventSave afterEvent;
	afterEvent.write(*uninterrupted->world, *uninterrupted->handler); // Only after the question finished.
	const auto completedBytes = afterEvent.contents();
	auto reloaded = ordinaryLoad(afterEvent);
	const auto restored = saveParity::capture(*reloaded->handler);
	EXPECT_EQ(saveParity::firstDifference(completed, restored), "");
	EXPECT_EQ(restored.rawStatistics, completed.rawStatistics);
	EXPECT_EQ(reloaded->world->getMap().scriptSource, uninterrupted->world->getMap().scriptSource);
	EXPECT_EQ(reloaded->world->getMap().getScriptVariables().get(ModScope::scopeMap(), "counter").Float(), firstBound - 3);
	const int secondBound = expected.nextInt(2, 3);
	EventCallback nextOriginal(*uninterrupted->world, uninterrupted->handler->getRandomGenerator());
	EventCallback nextLoaded(*reloaded->world, reloaded->handler->getRandomGenerator());
	ASSERT_NO_FATAL_FAILURE(completeQuestion(*uninterrupted->world, nextOriginal, boxId, heroId, secondBound));
	ASSERT_NO_FATAL_FAILURE(completeQuestion(*reloaded->world, nextLoaded, boxId, heroId, secondBound));
	EXPECT_EQ(nextLoaded.packets, nextOriginal.packets);
	const auto next = saveParity::capture(*uninterrupted->handler);
	EXPECT_EQ(saveParity::firstDifference(next, saveParity::capture(*reloaded->handler)), "");
	EXPECT_NE(next.channels.at("gameRandomizer"), completed.channels.at("gameRandomizer"));
	EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(reloaded->handler->getRandomGenerator())), bytes(expected));
	EXPECT_EQ(bytes(CRandomGenerator::getDefault()), defaultRng);
	EXPECT_EQ(beforeEvent.contents(), original);
	EXPECT_EQ(afterEvent.contents(), completedBytes);
}

}
