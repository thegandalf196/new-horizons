/*
 * NewHorizonsAuthoredMagicMapTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/mapping/CMapService.h"
#include "../../../lib/filesystem/CZipLoader.h"
#include "../../../lib/filesystem/CZipSaver.h"
#include "../../../lib/json/JsonUtils.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/constants/StringConstants.h"
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>

namespace
{
// Keep production buffer autodetection for BOTH vmap header and full map.
class AuthoredVmapService final : public CMapService
{
	std::vector<uint8_t> bytes;
	MapListener & listener;
public:
	AuthoredVmapService(std::vector<uint8_t> bytes, MapListener & listener)
		: bytes(std::move(bytes)), listener(listener) {}
	std::unique_ptr<CMap> loadMap(const ResourcePath & name, IGameInfoCallback * cb) const override
	{
		auto result = CMapService::loadMap(bytes.data(), static_cast<int>(bytes.size()), name.getName(), "core", "english", cb);
		listener.mapLoaded(result.get()); // Observe only; no authored-setting injection.
		return result;
	}
	std::unique_ptr<CMapHeader> loadMapHeader(const ResourcePath & name, bool /*isEditor*/ = false) const override
	{
		return CMapService::loadMapHeader(bytes.data(), static_cast<int>(bytes.size()), name.getName(), "core", "english");
	}
};
}

// No installed-baseline replacement and no mapLoaded rule injection. The vmap
// writer persists the authored setting; the production loader/new-game entrypoint
// must honor it under the real installed70 baseline.
class NewHorizonsAuthoredMagicMapTest : public TinyMapGameTest
{
protected:
	JsonNode installed;
	JsonNode authoredHeader;
	SpellID missile;
	boost::filesystem::path path;
	std::unique_ptr<AuthoredVmapService> authoredService;
	Services * gameServices() override { return LIBRARY; }

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		const auto * flag = std::getenv("NH_REQUIRE_MANAGED_MISSILE_PROFILE");
		ASSERT_NE(flag, nullptr);
		ASSERT_EQ(std::string(flag), "1");
		installed = LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS);
		ASSERT_EQ(installed["rulesetVersion"].Integer(), 2);
		ASSERT_EQ(installed["spells"].Struct().size(), 70u);
		missile = SpellID(SpellID::decode(GameConstants::NEW_HORIZONS_MAGIC_MISSILE));
		ASSERT_NE(missile, SpellID(SpellID::NONE));
		ASSERT_NE(missile, SpellID(SpellID::MAGIC_ARROW));
		path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("nh-authored-magic-%%%%-%%%%.vmap");
	}
	void TearDown() override
	{
		TinyMapGameTest::TearDown();
		authoredService.reset();
		if(!path.empty())
		{
			boost::system::error_code error;
			boost::filesystem::remove(path, error);
		}
	}

	void writeAuthored(const JsonNode * overrideRules)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Authored magic override")
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 1}});
		MapServiceTinyH3M original(builder.build(), nullptr);
		auto authored = original.loadMap(ResourcePath("authored"), gameState().get());
		ASSERT_NE(authored, nullptr);
		if(overrideRules)
			authored->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, *overrideRules);
		const auto copied = authored->getMagicOverride();
		ASSERT_EQ(copied.has_value(), overrideRules != nullptr);
		if(overrideRules)
			ASSERT_EQ(*copied, *overrideRules);
		CMapService writer;
		writer.saveMap(authored, path);
		{
			CZipLoader archive("", path);
			const auto data = archive.load(JsonPath::builtin("header.json"))->readAll();
			authoredHeader = JsonNode(reinterpret_cast<const std::byte *>(data.first.get()), data.second, "header.json");
		}
		readAuthoredBytes();
		EXPECT_EQ(LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS), installed);
	}

	void readAuthoredBytes()
	{
		std::ifstream input(path.string(), std::ios::binary);
		ASSERT_TRUE(input.is_open());
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		ASSERT_FALSE(bytes.empty());
		ASSERT_LE(bytes.size(), static_cast<size_t>(std::numeric_limits<int>::max()));
		authoredService = std::make_unique<AuthoredVmapService>(std::move(bytes), *this);
	}

	void expectShapeRejection()
	{
		for(const bool headerOnly : {true, false})
		{
			bool rejected = false;
			try
			{
				if(headerOnly)
					authoredService->loadMapHeader(ResourcePath("authored"));
				else
					authoredService->loadMap(ResourcePath("authored"), gameState().get());
			}
			catch(const std::runtime_error & error)
			{
				rejected = true;
				EXPECT_EQ(std::string(error.what()), "Invalid authored New Horizons magic context shape");
			}
			EXPECT_TRUE(rejected);
		}
	}

	void initializeAuthored()
	{
		StartInfo info;
		info.mapname = "authored";
		info.mode = EStartMode::NEW_GAME;
		info.difficulty = static_cast<ui8>(EMapDifficulty::EASY);
		const auto header = authoredService->loadMapHeader(ResourcePath(info.mapname));
		ASSERT_NE(header, nullptr);
		for(int index = 0; index < static_cast<int>(header->players.size()); ++index)
		{
			const auto & player = header->players[index];
			if(!player.canHumanPlay && !player.canComputerPlay)
				continue;
			auto & settings = info.playerInfos[PlayerColor(index)];
			settings.color = PlayerColor(index);
			settings.connectedPlayerIDs.insert(static_cast<PlayerConnectionID>(index));
			settings.name = "Player";
			settings.castle = player.defaultCastle();
			settings.hero = player.defaultHero();
			settings.bonus = PlayerStartingBonus::GOLD;
		}
		GameRandomizer randomizer(*gameState());
		Load::ProgressAccumulator progress;
		gameState()->init(authoredService.get(), &info, randomizer, progress, false);
		ASSERT_NE(map(), nullptr);
	}
};

TEST_F(NewHorizonsAuthoredMagicMapTest, FullAuthoredV1MapOverridesInstalled70AsAnExactContext)
{
	const JsonNode old69(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_EQ(old69["rulesetVersion"].Integer(), 1);
	ASSERT_EQ(old69["spells"].Struct().size(), 69u);
	ASSERT_NO_FATAL_FAILURE(writeAuthored(&old69));
	ASSERT_EQ(authoredHeader["versionMajor"].Integer(), 4);
	ASSERT_EQ(authoredHeader["newHorizonsMagic"], old69);
	ASSERT_NO_THROW(initializeAuthored());
	EXPECT_EQ(gameState()->getMagicRules(), old69);
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), missile));
	EXPECT_EQ(LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS), installed);
}

TEST_F(NewHorizonsAuthoredMagicMapTest, AbsentMapOverrideRetainsInstalled70)
{
	ASSERT_NO_FATAL_FAILURE(writeAuthored(nullptr));
	ASSERT_EQ(authoredHeader["versionMajor"].Integer(), 3);
	ASSERT_FALSE(authoredHeader.Struct().contains("newHorizonsMagic"));
	ASSERT_NO_THROW(initializeAuthored());
	EXPECT_EQ(gameState()->getMagicRules(), installed);
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), missile));
}

TEST_F(NewHorizonsAuthoredMagicMapTest, PartialMagicContextCannotBorrowInstalledVersionAndRoster)
{
	JsonNode partial;
	partial["spells"]["core:magicArrow"]["level"].Integer() = 2;
	ASSERT_NO_FATAL_FAILURE(writeAuthored(&partial));
	expectShapeRejection();
	EXPECT_THROW(initializeAuthored(), std::runtime_error);
	EXPECT_EQ(LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS), installed);
}

TEST_F(NewHorizonsAuthoredMagicMapTest, ExplicitNullSurvivesHeaderAndInitializesLegacyMagic)
{
	const JsonNode disabled;
	ASSERT_NO_FATAL_FAILURE(writeAuthored(&disabled));
	ASSERT_EQ(authoredHeader["versionMajor"].Integer(), 4);
	ASSERT_TRUE(authoredHeader.Struct().contains("newHorizonsMagic"));
	ASSERT_TRUE(authoredHeader["newHorizonsMagic"].isNull());
	ASSERT_NO_THROW(initializeAuthored());
	ASSERT_TRUE(map()->getMagicOverride().has_value());
	EXPECT_TRUE(map()->getMagicOverride()->isNull());
	EXPECT_TRUE(gameState()->getMagicRules().isNull());
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), missile));
	EXPECT_EQ(LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS), installed);
}

TEST_F(NewHorizonsAuthoredMagicMapTest, FullAuthoredV2SurvivesWriterAndNewGameCapture)
{
	ASSERT_NO_FATAL_FAILURE(writeAuthored(&installed));
	ASSERT_EQ(authoredHeader["versionMajor"].Integer(), 4);
	ASSERT_EQ(authoredHeader["newHorizonsMagic"], installed);
	ASSERT_NO_THROW(initializeAuthored());
	EXPECT_EQ(gameState()->getMagicRules(), installed);
	auto copy = map()->getMagicOverride();
	ASSERT_TRUE(copy.has_value());
	(*copy)["spells"].Struct().clear();
	EXPECT_EQ(*map()->getMagicOverride(), installed);
}

TEST_F(NewHorizonsAuthoredMagicMapTest, HeaderVersionGatesRejectOldTaggedAndUnknownFutureFormats)
{
	const JsonNode disabled;
	ASSERT_NO_FATAL_FAILURE(writeAuthored(&disabled));
	for(const int version : {3, 5})
	{
		authoredHeader["versionMajor"].Integer() = version;
		// Header-only archive suffices because version checks precede every
		// other entry. Require the exact version error, not a missing-file error.
		{
			CZipSaver archive(std::make_shared<CDefaultIOApi>(), path);
			const auto text = authoredHeader.toString();
			auto output = archive.addFile("header.json");
			output->write(reinterpret_cast<const uint8_t *>(text.data()), text.size());
		}
		ASSERT_NO_FATAL_FAILURE(readAuthoredBytes());
		bool rejected = false;
		try
		{
			authoredService->loadMapHeader(ResourcePath("authored"));
		}
		catch(const std::runtime_error & error)
		{
			rejected = true;
			EXPECT_EQ(std::string(error.what()), version == 3
				? "Authored New Horizons magic context requires map format version 4"
				: "Unsupported map format version");
		}
		EXPECT_TRUE(rejected);
	}
}

TEST_F(NewHorizonsAuthoredMagicMapTest, NamedFieldSchemaAcceptsOnlyNullOrCompleteSupportedContexts)
{
	const auto valid = [](const JsonNode & value)
	{
		return JsonUtils::validate(value, "vcmi:newHorizonsMapMagicOverride", "native authored magic field schema");
	};
	EXPECT_TRUE(valid(JsonNode()));
	EXPECT_TRUE(valid(JsonNode(JsonPath::builtin("config/newHorizonsMagic"))));
	EXPECT_TRUE(valid(installed));
	EXPECT_FALSE(valid(JsonNode(true)));
	EXPECT_FALSE(valid(JsonNode(4)));
	JsonNode partial;
	partial["rulesetVersion"].Integer() = 1;
	EXPECT_FALSE(valid(partial));
	auto malformed = installed;
	malformed["spells"][GameConstants::NEW_HORIZONS_MAGIC_MISSILE]["directDamage"] = JsonNode();
	EXPECT_FALSE(valid(malformed));
}

TEST_F(NewHorizonsAuthoredMagicMapTest, SettingsBinaryRoundTripDistinguishesAbsentNullAndObjectOverrides)
{
	for(const int mode : {0, 1, 2})
	{
		SCOPED_TRACE(mode);
		GameSettings source;
		source.loadBase(LIBRARY->settingsHandler->getFullConfig());
		const JsonNode old69(JsonPath::builtin("config/newHorizonsMagic"));
		if(mode != 0)
		{
			JsonNode authored;
			authored["magic"]["newHorizons"] = mode == 1 ? JsonNode() : old69;
			source.loadOverrides(authored);
		}
		CMemorySerializer memory;
		memory.oser & source;
		GameSettings restored;
		restored.loadBase(LIBRARY->settingsHandler->getFullConfig());
		memory.iser & restored;
		ASSERT_EQ(restored.getMagicOverride().has_value(), mode != 0);
		const auto expected = mode == 0 ? installed : (mode == 1 ? JsonNode() : old69);
		EXPECT_EQ(restored.getValue(EGameSettings::MAGIC_NEW_HORIZONS), expected);
		if(mode != 0)
			EXPECT_EQ(*restored.getMagicOverride(), expected);
	}
}

TEST_F(NewHorizonsAuthoredMagicMapTest, MalformedFieldTypesRejectInActualHeaderAndFullReaders)
{
	for(const auto & malformed : {JsonNode(true), JsonNode(7), JsonNode("invalid")})
	{
		ASSERT_NO_FATAL_FAILURE(writeAuthored(&malformed));
		expectShapeRejection();
	}
}

TEST_F(NewHorizonsAuthoredMagicMapTest, SchemaValidMissingMissileFormulaRejectsOnlyAtRuntimeCapture)
{
	auto missing = installed;
	missing["spells"][GameConstants::NEW_HORIZONS_MAGIC_MISSILE].Struct().erase("directDamage");
	ASSERT_TRUE(JsonUtils::validate(missing, "vcmi:newHorizonsMapMagicOverride", "shape versus runtime semantic boundary"));
	ASSERT_NO_FATAL_FAILURE(writeAuthored(&missing));
	ASSERT_NO_THROW(authoredService->loadMapHeader(ResourcePath("authored")));
	EXPECT_THROW(initializeAuthored(), std::runtime_error);
	EXPECT_EQ(LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS), installed);
}

TEST_F(NewHorizonsAuthoredMagicMapTest, OtherSettingsRetainMergeAndNullOmission)
{
	GameSettings settings;
	settings.loadBase(LIBRARY->settingsHandler->getFullConfig());
	const auto original = settings.getValue(EGameSettings::HEROES_NEW_HORIZONS);
	JsonNode nullOverride;
	nullOverride["heroes"]["newHorizons"] = JsonNode();
	settings.loadOverrides(nullOverride);
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS), original);
	JsonNode patch;
	patch["maxPrimary"].Integer() = 9000;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, patch);
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS)["maxPrimary"].Integer(), 9000);
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS)["classProfiles"], original["classProfiles"]);
	EXPECT_FALSE(settings.getMagicOverride().has_value());
}
