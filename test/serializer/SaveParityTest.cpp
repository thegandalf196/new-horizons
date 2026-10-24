/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../tools/native/SaveParityInput.h"
#include "../../tools/native/SaveParityEvidence.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/CLoadFile.h"
#include "../../lib/serializer/CSaveFile.h"
#include "../../server/CGameHandler.h"
#include "../../server/TurnTimerHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/processors/HeroPoolProcessor.h"
#include "../../server/processors/PlayerMessageProcessor.h"
#include "../../server/processors/TurnOrderProcessor.h"
#include "../../lib/campaign/CampaignState.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include <fstream>
#include <exception>
#include <vstd/CLoggerBase.h>

namespace
{
template<typename T> std::vector<std::byte> serialize(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

std::string fieldDifference(const char * name, const std::vector<std::byte> & live,
	const std::vector<std::byte> & decoded)
{
	const auto mismatch = std::mismatch(live.begin(), live.end(), decoded.begin(), decoded.end());
	const bool equal = mismatch.first == live.end() && mismatch.second == decoded.end();
	return std::string(name) + "{liveBytes=" + std::to_string(live.size())
		+ ",decodedBytes=" + std::to_string(decoded.size()) + ",firstMismatch="
		+ (equal ? "equal" : std::to_string(std::distance(live.begin(), mismatch.first))) + "}";
}

std::string byteWindow(const std::vector<std::byte> & data, size_t position)
{
	const auto begin = std::min(data.size(), position > 16 ? position - 16 : 0);
	const auto end = std::min(data.size(), position + 17);
	std::string result = "[" + std::to_string(begin) + "," + std::to_string(end) + ")=";
	constexpr char hex[] = "0123456789abcdef";
	for(size_t index = begin; index < end; ++index)
	{
		const auto value = std::to_integer<unsigned char>(data[index]);
		result += hex[value >> 4];
		result += hex[value & 15];
	}
	return result;
}

std::string worldDiagnostic(const CGameState & live, const CGameState & decoded,
	const std::vector<std::byte> & liveWorld, const std::vector<std::byte> & decodedWorld)
{
	const auto mismatch = std::mismatch(liveWorld.begin(), liveWorld.end(), decodedWorld.begin(), decodedWorld.end());
	const auto position = static_cast<size_t>(std::distance(liveWorld.begin(), mismatch.first));
	std::string result = fieldDifference("world", liveWorld, decodedWorld)
		+ "\nworldLiveWindow" + byteWindow(liveWorld, position)
		+ "\nworldDecodedWindow" + byteWindow(decodedWorld, position);
	result += "\n" + fieldDifference("startOptions", serialize(*live.getStartInfo()), serialize(*decoded.getStartInfo()));
	result += "\n" + fieldDifference("initialOptions", serialize(*live.getInitialStartInfo()), serialize(*decoded.getInitialStartInfo()));
	result += "\n" + fieldDifference("mapHeader", serialize(*live.getMapHeader()), serialize(*decoded.getMapHeader()));
	result += "\n" + fieldDifference("mapTexts", serialize(*live.getMapHeader()->texts), serialize(*decoded.getMapHeader()->texts));
	return result;
}

struct TemporarySave
{
	boost::filesystem::path directory = boost::filesystem::temp_directory_path()
		/ boost::filesystem::unique_path("nh-save-parity-%%%%%%%%-%%%%%%%%");
	boost::filesystem::path path = directory / "synthetic.vsgm1";
	TemporarySave() { boost::filesystem::create_directory(directory); }
	~TemporarySave() { boost::filesystem::remove_all(directory); }
	std::string contents() const
	{
		std::ifstream input(path.string(), std::ios::binary);
		return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	}
};

// Explicit native serialization fixture: pin every current randomizer field,
// including private saved streams, without rolling gameplay to populate them.
struct HeroSkillImage
{
	CRandomGenerator seed{3};
	int8_t magicSchoolCounter = 1;
	int8_t wisdomCounter = 1;
	template<typename H> void serialize(H & h)
	{
		h & seed;
		h & magicSchoolCounter;
		h & wisdomCounter;
	}
};

struct RandomizerImage
{
	CRandomGenerator global{42};
	std::map<ArtifactID, int> artifacts{{ArtifactID(0), 1}};
	std::map<HeroTypeID, HeroSkillImage> skills;
	// goodMorale, badMorale, goodLuck, badLuck, combatAbility, heroLuck, heroMorale
	std::array<std::map<ObjectInstanceID, RandomGeneratorWithBias>, 7> streams;
	RandomizerImage()
	{
		skills.try_emplace(HeroTypeID(0));
		for(size_t index = 0; index < streams.size(); ++index)
			streams[index].try_emplace(ObjectInstanceID(0), static_cast<int>(index + 8));
	}
	template<typename H> void serialize(H & h)
	{
		h & global;
		h & artifacts;
		h & skills;
		for(auto & stream : streams)
			h & stream;
	}
	void install(GameRandomizer & target) const
	{
		CMemorySerializer image;
		image.oser & *this;
		image.iser & target;
	}
};

class ChunkExecutionLog final : public vstd::CLoggerBase
{
	vstd::CLoggerBase * previous = logMod;
	mutable std::vector<std::string> messages;
public:
	ChunkExecutionLog() { logMod = this; }
	~ChunkExecutionLog() override { logMod = previous; }
	void log(ELogLevel::ELogLevel, const std::string & message) const override { messages.push_back(message); }
	void log(ELogLevel::ELogLevel level, const boost::format & format) const override { log(level, format.str()); }
	ELogLevel::ELogLevel getEffectiveLevel() const override { return ELogLevel::TRACE; }
	bool isDebugEnabled() const override { return true; }
	bool isTraceEnabled() const override { return true; }
	size_t executions() const
	{
		return std::count_if(messages.begin(), messages.end(), [](const auto & message)
		{
			return message.find("failed to run:") != std::string::npos
				&& message.find("NH_SAVE_PARITY_CHUNK_EXECUTED") != std::string::npos;
		});
	}
};

saveParity::Evidence coverageStart()
{
	saveParity::Evidence result;
	result.day = 2;
	result.actingPlayers = {PlayerColor(0)};
	ReplayChapter chapter;
	chapter.day = 2;
	chapter.gamestateSnapshot = {std::byte{1}};
	chapter.packs = {{std::byte{2}}};
	chapter.packVersions = {ESerializationVersion::NONE};
	ReplayTurnMark mark;
	mark.player = PlayerColor(0);
	chapter.turns.push_back(mark);
	result.chapters.push_back(chapter);
	return result;
}

saveParity::Evidence coverageFinish(const saveParity::Evidence & start)
{
	auto finish = start;
	++finish.day;
	finish.chapters[0].packs.push_back({std::byte{3}});
	finish.chapters[0].packVersions.push_back(ESerializationVersion::NONE);
	finish.chapters[0].turns[0].lastPack = 2;
	auto next = start.chapters[0];
	next.day = finish.day;
	finish.chapters.push_back(next);
	return finish;
}
}

class SaveParityTest : public TinyMapGameTest
{
	NativeBonusGraphAccess access;
protected:
	saveParity::InspectionServer server;
	std::unique_ptr<CGameHandler> handler;
	Services * gameServices() override { return LIBRARY; }
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0));
		startWithMap(std::move(builder));
		// Match native map/save header registration before taking the initial snapshot.
		const auto mapTexts = map()->texts;
		ASSERT_NE(mapTexts.get(), static_cast<TextLocalizationContainer *>(LIBRARY->generaltexth.get()));
		std::map<std::string, ExportedStrings> originalTexts;
		mapTexts->exportAllTexts(originalTexts, false);
		ASSERT_EQ(originalTexts.size(), 1);
		ASSERT_TRUE(originalTexts.contains("core"));
		ASSERT_FALSE(originalTexts.at("core").strings.empty());
		EXPECT_TRUE(originalTexts.at("core").overridenMods.empty());
		const auto translations = serialize(map()->translations);
		map()->registerMapStrings();
		EXPECT_EQ(map()->texts, mapTexts);
		std::map<std::string, ExportedStrings> registeredTexts;
		mapTexts->exportAllTexts(registeredTexts, false);
		ASSERT_EQ(registeredTexts.size(), 1);
		ASSERT_TRUE(registeredTexts.contains("map"));
		EXPECT_EQ(registeredTexts.at("map").strings, originalTexts.at("core").strings);
		EXPECT_TRUE(registeredTexts.at("map").overridenMods.empty());
		EXPECT_EQ(serialize(map()->translations), translations);
		const auto registeredNativeTexts = serialize(*mapTexts);
		map()->registerMapStrings();
		EXPECT_EQ(map()->texts, mapTexts);
		EXPECT_EQ(serialize(*mapTexts), registeredNativeTexts);
		EXPECT_EQ(serialize(map()->translations), translations);
		gameState()->actingPlayers = {PlayerColor(0)};
		gameState()->players.at(PlayerColor(0)).human = true;
		handler = std::make_unique<CGameHandler>(server, gameState());
		handler->randomizer->setSeed(42);
		gameState()->replayLog.configure(false, 1);
		gameState()->replayLog.beginDay(gameState()->day, gameState()->saveReplaySnapshot());
		gameState()->replayLog.addTurn(PlayerColor(0));
	}
	void TearDown() override
	{
		handler.reset();
		TinyMapGameTest::TearDown();
	}
	void write(const TemporarySave & save)
	{
		CSaveFile file;
		gameState()->saveGame(file);
		file.save(*handler);
		try { file.write(save.path); }
		catch(const std::exception & error)
		{
			boost::system::error_code sizeError;
			const auto written = boost::filesystem::file_size(save.path, sizeError);
			std::throw_with_nested(std::runtime_error("Synthetic save file write (bufferBytes="
				+ std::to_string(file.currentContent().size()) + ", fileBytes="
				+ (sizeError ? "unavailable" : std::to_string(written)) + "): " + error.what()));
		}
		const auto actual = save.contents();
		const auto & expected = file.currentContent();
		ASSERT_EQ(actual.size(), expected.size()) << "Synthetic save bytes differ from native writer buffer";
		EXPECT_TRUE(std::equal(expected.begin(), expected.end(), actual.begin(), [](std::byte left, char right)
		{
			return std::to_integer<unsigned char>(left) == static_cast<unsigned char>(right);
		})) << "Synthetic save bytes differ from native writer buffer";
	}
};

TEST_F(SaveParityTest, OrdinaryEnvelopeDecodesAllServerStateWithoutChangingInput)
{
	TemporarySave save;
	write(save);
	const auto original = save.contents();
	const auto expected = saveParity::capture(*handler);
	auto first = saveParity::readSave(save.path);
	auto second = saveParity::readSave(save.path);
	EXPECT_EQ(first->version, ESerializationVersion::CURRENT);
	EXPECT_EQ(saveParity::firstDifference(expected, saveParity::capture(*first->handler)), "")
		<< worldDiagnostic(*gameState(), *first->world, expected.channels.at("world"), first->world->saveToMemory());
	EXPECT_EQ(saveParity::firstDifference(saveParity::capture(*first->handler),
		saveParity::capture(*second->handler)), "");
	EXPECT_EQ(save.contents(), original);
	EXPECT_EQ(first->world->getMapEventDispatcher(), nullptr);
	EXPECT_EQ(first->world->getHeroCommandRules(), gameState()->getHeroCommandRules());
	EXPECT_EQ(first->world->getMagicRules(), gameState()->getMagicRules());
	EXPECT_EQ(first->world->getHeroPerkRules(), gameState()->getHeroPerkRules());
}

TEST_F(SaveParityTest, MapChunkIsNotConstructedByInspectionButNormalLoadRetainsDispatcher)
{
	map()->scriptSource = "error('NH_SAVE_PARITY_CHUNK_EXECUTED'); return {}";
	TemporarySave save;
	write(save);
	ChunkExecutionLog execution;
	auto decoded = saveParity::readSave(save.path);
	EXPECT_EQ(execution.executions(), 0);
	EXPECT_EQ(decoded->world->getMapEventDispatcher(), nullptr);
	EXPECT_EQ(decoded->world->getMap().scriptSource, map()->scriptSource);
	saveParity::LoadedSave ordinary;
	CLoadFile file(save.path, ordinary.world.get());
	ordinary.world->loadGame(file);
	EXPECT_NE(ordinary.world->getMapEventDispatcher(), nullptr);
	EXPECT_EQ(execution.executions(), 1);
}

TEST_F(SaveParityTest, OnlyStatisticsTimestampsAreNormalizedAndOwnerIsRestored)
{
	StatisticDataSetEntry entry{};
	entry.timestamp = 12;
	handler->statistics->data.push_back(entry);
	const auto before = saveParity::capture(*handler);
	handler->statistics->data[0].timestamp = 24;
	const auto clockOnly = saveParity::capture(*handler);
	EXPECT_NE(before.rawStatistics, clockOnly.rawStatistics);
	EXPECT_EQ(saveParity::firstDifference(before, clockOnly), "");
	EXPECT_EQ(handler->statistics->data[0].timestamp, 24);
	handler->statistics->data[0].numBattlesNeutral += 1;
	EXPECT_NE(saveParity::firstDifference(clockOnly, saveParity::capture(*handler)), "");
}

TEST_F(SaveParityTest, WorldQueryAndReplayMutationsAreNeverMasked)
{
	const auto before = saveParity::capture(*handler);
	gameState()->players.at(PlayerColor(0)).resources[size_t(0)] += 1;
	EXPECT_NE(before.channels.at("world"), saveParity::capture(*handler).channels.at("world"));
	const auto worldChanged = saveParity::capture(*handler);
	handler->QID = QueryID(99);
	EXPECT_NE(worldChanged.channels.at("fullServerWithoutStatisticsTimestamps"),
		saveParity::capture(*handler).channels.at("fullServerWithoutStatisticsTimestamps"));
	const auto ledger = serialize(gameState()->replayLog);
	gameState()->replayLog.addPack({std::byte{77}});
	const auto replayChanged = saveParity::capture(*handler);
	EXPECT_NE(worldChanged.channels.at("replay"), replayChanged.channels.at("replay"));
	EXPECT_NE(ledger, serialize(gameState()->replayLog));
	EXPECT_EQ(replayChanged.channels.at("replay"), serialize(gameState()->replayLog));
}

TEST_F(SaveParityTest, FullGameRandomizerAndHeroPoolStreamsAreRetained)
{
	const auto before = saveParity::capture(*handler);
	handler->randomizer->getDefault().nextInt();
	EXPECT_NE(before.channels.at("gameRandomizer"), saveParity::capture(*handler).channels.at("gameRandomizer"));
	std::map<PlayerColor, std::unique_ptr<CRandomGenerator>> seeds;
	seeds.emplace(PlayerColor(0), std::make_unique<CRandomGenerator>(17));
	CMemorySerializer image;
	image.oser & seeds;
	image.iser & *handler->heroPool;
	const auto first = saveParity::capture(*handler);
	EXPECT_EQ(first.channels.at("heroPoolRandomizers"), serialize(seeds));
	seeds.at(PlayerColor(0))->nextInt();
	CMemorySerializer changed;
	changed.oser & seeds;
	changed.iser & *handler->heroPool;
	EXPECT_NE(first.channels.at("heroPoolRandomizers"), saveParity::capture(*handler).channels.at("heroPoolRandomizers"));
}

TEST_F(SaveParityTest, TimedGamesAndPublicationAreRejected)
{
	InfoWindow pack;
	EXPECT_THROW(server.applyPack(pack), std::runtime_error);
	EXPECT_THROW(server.sendPack(pack, GameConnectionID::FIRST_CONNECTION), std::runtime_error);
	EXPECT_THROW(server.setState(EServerState::GAMEPLAY), std::runtime_error);
	gameState()->getStartInfo()->turnTimerInfo.unitTimer = 1;
	EXPECT_THROW(saveParity::capture(*handler), std::runtime_error);
}

TEST_F(SaveParityTest, EverySavedRandomizerStreamCounterAndBiasIsCompared)
{
	for(size_t field = 0; field < 12; ++field)
	{
		SCOPED_TRACE(field);
		RandomizerImage image;
		image.install(*handler->randomizer);
		EXPECT_EQ(serialize(*handler->randomizer), serialize(image));
		const auto original = saveParity::capture(*handler);
		if(field == 0)
			image.global.nextInt();
		else if(field == 1)
			++image.artifacts.at(ArtifactID(0));
		else if(field == 2)
			image.skills.at(HeroTypeID(0)).seed.nextInt();
		else if(field == 3)
			++image.skills.at(HeroTypeID(0)).magicSchoolCounter;
		else if(field == 4)
			++image.skills.at(HeroTypeID(0)).wisdomCounter;
		else
			image.streams[field - 5].at(ObjectInstanceID(0)).roll(50, 100, 10);
		image.install(*handler->randomizer);
		EXPECT_EQ(serialize(*handler->randomizer), serialize(image));
		EXPECT_NE(original.channels.at("gameRandomizer"),
			saveParity::capture(*handler).channels.at("gameRandomizer"));
		EXPECT_EQ(saveParity::capture(*handler).channels.at("gameRandomizer"), serialize(image));
	}
}

TEST_F(SaveParityTest, TruncatedAndTrailingSavePayloadsAreRejected)
{
	TemporarySave save;
	write(save);
	const auto original = save.contents();
	{
		std::ofstream output(save.path.string(), std::ios::binary | std::ios::app);
		output.put('x');
	}
	EXPECT_THROW(saveParity::readSave(save.path), std::runtime_error);
	{
		std::ofstream output(save.path.string(), std::ios::binary | std::ios::trunc);
		output.write(original.data(), original.size() / 2);
	}
	EXPECT_THROW(saveParity::readSave(save.path), std::exception);
}

TEST(SaveParityCoverageTest, ExactStartingPrefixAndCompletedNextDayAreRequired)
{
	const auto start = coverageStart();
	const auto finish = coverageFinish(start);
	EXPECT_NO_THROW(saveParity::requireTurnCoverage(start, finish));
	auto bad = finish;
	bad.chapters.erase(bad.chapters.begin());
	EXPECT_THROW(saveParity::requireTurnCoverage(start, bad), std::runtime_error);
	bad = finish;
	bad.chapters[0].packs[0][0] = std::byte{9};
	EXPECT_THROW(saveParity::requireTurnCoverage(start, bad), std::runtime_error);
	bad = finish;
	bad.chapters[0].turns[0].lastPack = ReplayTurnMark::ongoingTurn;
	EXPECT_THROW(saveParity::requireTurnCoverage(start, bad), std::runtime_error);
	bad = finish;
	bad.chapters[1].turns.clear();
	EXPECT_THROW(saveParity::requireTurnCoverage(start, bad), std::runtime_error);
}

TEST(SaveParityCoverageTest, LeadingInitializationDayZeroAllowsDayOneToDayTwoCoverage)
{
	auto start = coverageStart();
	start.day = 1;
	start.chapters.front().day = 1;
	const auto ordinaryStart = start;
	const auto finish = coverageFinish(start);
	ReplayChapter initialization;
	initialization.day = 0;
	start.chapters.insert(start.chapters.begin(), initialization);
	EXPECT_NO_THROW(saveParity::requireTurnCoverage(start, finish));
	auto retainedFinish = finish;
	retainedFinish.chapters.insert(retainedFinish.chapters.begin(), initialization);
	EXPECT_NO_THROW(saveParity::requireTurnCoverage(start, retainedFinish));
	EXPECT_NO_THROW(saveParity::requireTurnCoverage(ordinaryStart, retainedFinish));
}

TEST(SaveParityCoverageTest, DuplicateDayZeroIsRejectedInEitherInput)
{
	const auto start = coverageStart();
	const auto finish = coverageFinish(start);
	ReplayChapter initialization;
	initialization.day = 0;
	auto badStart = start;
	badStart.chapters.insert(badStart.chapters.begin(), {initialization, initialization});
	EXPECT_THROW(saveParity::requireTurnCoverage(badStart, finish), std::runtime_error);
	auto badFinish = finish;
	badFinish.chapters.insert(badFinish.chapters.begin(), {initialization, initialization});
	EXPECT_THROW(saveParity::requireTurnCoverage(start, badFinish), std::runtime_error);
}

TEST(SaveParityCoverageTest, DescentToDayZeroIsStillRejected)
{
	auto start = coverageStart();
	const auto finish = coverageFinish(start);
	ReplayChapter preceding;
	preceding.day = 1;
	ReplayChapter initialization;
	initialization.day = 0;
	start.chapters.insert(start.chapters.begin(), {preceding, initialization});
	EXPECT_THROW(saveParity::requireTurnCoverage(start, finish), std::runtime_error);
}

TEST(SaveParityCoverageTest, SingleStartingChapterRemainsAccepted)
{
	const auto start = coverageStart();
	ASSERT_EQ(start.chapters.size(), 1);
	EXPECT_NO_THROW(saveParity::requireTurnCoverage(start, coverageFinish(start)));
}

TEST(SaveParityCoverageTest, EmptyChaptersRemainRejectedInEitherInput)
{
	const auto start = coverageStart();
	const auto finish = coverageFinish(start);
	auto badStart = start;
	badStart.chapters.clear();
	EXPECT_THROW(saveParity::requireTurnCoverage(badStart, finish), std::runtime_error);
	auto badFinish = finish;
	badFinish.chapters.clear();
	EXPECT_THROW(saveParity::requireTurnCoverage(start, badFinish), std::runtime_error);
}

TEST(SaveParityCoverageTest, ChangedPacketOrderCountAndMarksRemainDifferent)
{
	auto left = coverageFinish(coverageStart());
	auto right = left;
	left.channels["replay"] = serialize(left.chapters);
	std::swap(right.chapters[0].packs[0], right.chapters[0].packs[1]);
	right.channels["replay"] = serialize(right.chapters);
	EXPECT_NE(saveParity::firstDifference(left, right), "");
	right = left;
	right.chapters[0].packs.pop_back();
	right.channels["replay"] = serialize(right.chapters);
	EXPECT_NE(saveParity::firstDifference(left, right), "");
	right = left;
	right.chapters[0].turns[0].firstPack += 1;
	right.channels["replay"] = serialize(right.chapters);
	EXPECT_NE(saveParity::firstDifference(left, right), "");
}
