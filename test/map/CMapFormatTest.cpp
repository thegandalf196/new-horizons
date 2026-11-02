/*
 * CMapFormatTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/callback/EditorCallback.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapping/MapFormatH3M.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/JsonDeserializer.h"
#include "../../lib/serializer/JsonSerializer.h"

#include "../../lib/filesystem/CMemoryBuffer.h"
#include "../../lib/filesystem/Filesystem.h"

#include "../../lib/mapping/CMap.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/rmg/CMapGenerator.h"
#include "../../lib/mapping/MapFormatJson.h"

#include "../../lib/VCMIDirs.h"

#include "MapComparer.h"
#include "../JsonComparer.h"
#include "mock/ZoneOptionsFake.h"

static const int TEST_RANDOM_SEED = 1337;

static void saveTestMap(CMemoryBuffer & serializeBuffer, const std::string & filename)
{
	auto path = VCMIDirs::get().userDataPath() / filename;
	boost::filesystem::remove(path);
	std::ofstream tmp(path.c_str(), std::ofstream::binary);

	tmp.write((const char *)serializeBuffer.getBuffer().data(), serializeBuffer.getSize());
	tmp.flush();
	tmp.close();
}

TEST(MapFormat, DISABLED_Random)
{
	SCOPED_TRACE("MapFormat_Random start");

	CMapGenOptions opt;
	CRmgTemplate tmpl;
	auto zoneOptions = std::make_shared<ZoneOptionsFake>();

	const_cast<CRmgTemplate::CPlayerCountRange &>(tmpl.getHumanPlayers()).addRange(1, 4);
	const_cast<CRmgTemplate::Zones &>(tmpl.getZones())[0] = zoneOptions;

	zoneOptions->setOwner(1);
	opt.setMapTemplate(&tmpl);

	opt.setHeight(CMapHeader::MAP_SIZE_MIDDLE);
	opt.setWidth(CMapHeader::MAP_SIZE_MIDDLE);
	opt.setLevels(2);
	opt.setHumanOrCpuPlayerCount(4);

	opt.setPlayerTypeForStandardPlayer(PlayerColor(0), EPlayerType::HUMAN);
	opt.setPlayerTypeForStandardPlayer(PlayerColor(1), EPlayerType::AI);
	opt.setPlayerTypeForStandardPlayer(PlayerColor(2), EPlayerType::AI);
	opt.setPlayerTypeForStandardPlayer(PlayerColor(3), EPlayerType::AI);

	CMapGenerator gen(opt, nullptr, TEST_RANDOM_SEED);

	std::unique_ptr<CMap> initialMap = gen.generate();
	initialMap->name.appendRawString("Test");
	SCOPED_TRACE("MapFormat_Random generated");

	CMemoryBuffer serializeBuffer;
	{
		CMapSaverJson saver(&serializeBuffer);
		saver.saveMap(initialMap);
	}
	SCOPED_TRACE("MapFormat_Random serialized");
	saveTestMap(serializeBuffer, "test_random.vmap");
	SCOPED_TRACE("MapFormat_Random saved");

	serializeBuffer.seek(0);
	{
		CMapLoaderJson loader(&serializeBuffer);
		std::unique_ptr<CMap> serialized = loader.loadMap(nullptr);

		MapComparer c;
		c(serialized, initialMap);
	}
	SCOPED_TRACE("MapFormat_Random finish");
}

static JsonNode getFromArchive(CZipLoader & archive, const std::string & archiveFilename)
{
	JsonPath resource = JsonPath::builtin(archiveFilename);

	if(!archive.existsResource(resource))
		throw std::runtime_error(archiveFilename + " not found");

	auto data = archive.load(resource)->readAll();

	JsonNode res(reinterpret_cast<const std::byte *>(data.first.get()), data.second, resource.getName());

	return res;
}

static void addToArchive(CZipSaver & saver, const JsonNode & data, const std::string & filename)
{
	auto s = data.toString();
	std::unique_ptr<COutputStream> stream = saver.addFile(filename);

	if(stream->write((const ui8*)s.c_str(), s.size()) != s.size())
		throw std::runtime_error("CMapSaverJson::saveHeader() zip compression failed.");
}

static std::unique_ptr<CMap> loadOriginal(const JsonNode & header, const JsonNode & objects, const JsonNode & surface, const JsonNode & underground)
{
	CMemoryBuffer initialBuffer;

	std::shared_ptr<CIOApi> originalDataIO(new CProxyIOApi(&initialBuffer));

	{
		CZipSaver initialSaver(originalDataIO, "_");

		addToArchive(initialSaver, header, CMapFormatJson::HEADER_FILE_NAME);
		addToArchive(initialSaver, objects, CMapFormatJson::OBJECTS_FILE_NAME);
		addToArchive(initialSaver, surface, "surface_terrain.json");
		addToArchive(initialSaver, underground, "underground_terrain.json");
	}

	initialBuffer.seek(0);

	CMapLoaderJson initialLoader(&initialBuffer);

	return initialLoader.loadMap(nullptr);
}

static void loadActual(CMemoryBuffer * serializeBuffer, const std::unique_ptr<CMap> & originalMap, JsonNode & header, JsonNode & objects, JsonNode & surface, JsonNode & underground)
{
	{
		CMapSaverJson saver(serializeBuffer);
		saver.saveMap(originalMap);
	}

	std::shared_ptr<CIOApi> actualDataIO(new CProxyROIOApi(serializeBuffer));
	CZipLoader actualDataLoader("", "_", actualDataIO);

	header = getFromArchive(actualDataLoader, CMapFormatJson::HEADER_FILE_NAME);
	objects = getFromArchive(actualDataLoader, CMapFormatJson::OBJECTS_FILE_NAME);
	surface = getFromArchive(actualDataLoader, "surface_terrain.json");
	underground = getFromArchive(actualDataLoader, "underground_terrain.json");
}

TEST(MapFormat, DISABLED_Objects)
{
	static const std::string MAP_DATA_PATH = "test/ObjectPropertyTest/";

	const JsonNode initialHeader(JsonPath::builtin(MAP_DATA_PATH + CMapFormatJson::HEADER_FILE_NAME));
	const JsonNode expectedHeader(JsonPath::builtin(MAP_DATA_PATH + CMapFormatJson::HEADER_FILE_NAME));//same as initial for now

	const JsonNode initialObjects(JsonPath::builtin(MAP_DATA_PATH + CMapFormatJson::OBJECTS_FILE_NAME));
	const JsonNode expectedObjects(JsonPath::builtin(MAP_DATA_PATH + "objects.ex.json"));

	const JsonNode expectedSurface(JsonPath::builtin(MAP_DATA_PATH + "surface_terrain.json"));
	const JsonNode expectedUnderground(JsonPath::builtin(MAP_DATA_PATH + "underground_terrain.json"));

	std::unique_ptr<CMap> originalMap = loadOriginal(initialHeader, initialObjects, expectedSurface, expectedUnderground);

	CMemoryBuffer serializeBuffer;

	JsonNode actualHeader;
	JsonNode actualObjects;
	JsonNode actualSurface;
	JsonNode actualUnderground;

	loadActual(&serializeBuffer, originalMap, actualHeader, actualObjects, actualSurface, actualUnderground);

	saveTestMap(serializeBuffer, "test_object_property.vmap");

	{
		JsonComparer c(false);
		c.compare("hdr", actualHeader, expectedHeader);
		c.compare("obj", actualObjects, expectedObjects);
	}

	{
		JsonComparer c(true);
		c.compare("surface", actualSurface, expectedSurface);
		c.compare("underground", actualUnderground, expectedUnderground);
	}
}

TEST(MapFormat, DISABLED_Terrain)
{
	static const std::string MAP_DATA_PATH = "test/TerrainTest/";

	const JsonNode expectedHeader(JsonPath::builtin(MAP_DATA_PATH + CMapFormatJson::HEADER_FILE_NAME));
	const JsonNode expectedObjects(JsonPath::builtin(MAP_DATA_PATH + CMapFormatJson::OBJECTS_FILE_NAME));

	const JsonNode expectedSurface(JsonPath::builtin(MAP_DATA_PATH + "surface_terrain.json"));
	const JsonNode expectedUnderground(JsonPath::builtin(MAP_DATA_PATH + "underground_terrain.json"));

	std::unique_ptr<CMap> originalMap = loadOriginal(expectedHeader, expectedObjects, expectedSurface, expectedUnderground);

	CMemoryBuffer serializeBuffer;

	JsonNode actualHeader;
	JsonNode actualObjects;
	JsonNode actualSurface;
	JsonNode actualUnderground;

	loadActual(&serializeBuffer, originalMap, actualHeader, actualObjects, actualSurface, actualUnderground);

	saveTestMap(serializeBuffer, "test_terrain.vmap");

	{
		JsonComparer c(false);
		c.compare("hdr", actualHeader, expectedHeader);
		c.compare("obj", actualObjects, expectedObjects);
	}

	{
		JsonComparer c(true);
		c.compare("surface", actualSurface, expectedSurface);
		c.compare("underground", actualUnderground, expectedUnderground);
	}
}

namespace
{
class MapScrollRoundTripTest : public ::testing::TestWithParam<std::string>
{
};

TEST_P(MapScrollRoundTripTest, KeepsExplicitSpellIdentity)
{
	if(GetParam().starts_with("new-horizons:")
		&& !vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the separate New Horizons native profile";
	const SpellID expected(SpellID::decode(GetParam()));
	ASSERT_TRUE(expected.hasValue());
	auto bytes = TinyH3M::TinyH3MBuilder(EMapFormat::SOD)
		.size(36, false).name("ScrollIdentityRoundTrip")
		.scroll({10, 10, 0}, SpellID(SpellID::MAGIC_ARROW)).build();
	CMemoryBuffer seed;
	seed.write(bytes.data(), static_cast<si64>(bytes.size()));
	seed.seek(0);
	EditorCallback sourceCallback(nullptr);
	CMapLoaderH3M sourceLoader("ScrollIdentityRoundTrip", "core", "ASCII", &seed);
	auto source = sourceLoader.loadMap(&sourceCallback);
	ASSERT_NE(source, nullptr);
	sourceCallback.setMap(source.get());
	ASSERT_EQ(source->objects.size(), 1u);
	auto * sourceScroll = dynamic_cast<CGArtifact *>(source->objects.front().get());
	ASSERT_NE(sourceScroll, nullptr);
	sourceScroll->setArtifactInstance(source->createArtifact(ArtifactID::SPELL_SCROLL, expected));
	ASSERT_EQ(sourceScroll->getArtifactInstance()->getScrollSpellID(), expected);

	CMemoryBuffer serialized;
	{
		CMapSaverJson saver(&serialized);
		saver.saveMap(source);
	}
	serialized.seek(0);
	EditorCallback restoredCallback(nullptr);
	CMapLoaderJson loader(&serialized);
	auto restored = loader.loadMap(&restoredCallback);
	ASSERT_NE(restored, nullptr);
	restoredCallback.setMap(restored.get());
	ASSERT_EQ(restored->objects.size(), 1u);
	const auto * restoredScroll = dynamic_cast<const CGArtifact *>(restored->objects.front().get());
	ASSERT_NE(restoredScroll, nullptr);
	ASSERT_NE(restoredScroll->getArtifactInstance(), nullptr);
	EXPECT_EQ(restoredScroll->getArtifactInstance()->getScrollSpellID(), expected);
}

INSTANTIATE_TEST_SUITE_P(CoreAndNewHorizons, MapScrollRoundTripTest,
	::testing::Values("core:magicArrow", "new-horizons:theChosen", "new-horizons:shadowGift"));

class TownMapDefaultsRoundTripTest : public TinyMapGameTest, public ::testing::WithParamInterface<int>
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto * town = loaded->getObjects<CGTownInstance>().front();
		town->removeAllBuildings();
		if(GetParam() < 2) town->addBuilding(BuildingID::DEFAULT);
		if(GetParam() != 1) town->addBuilding(BuildingID::FORT);
		if(GetParam() == 3)
		{
			town->addBuilding(BuildingID::MAGES_GUILD_1);
			town->forbiddenBuildings.insert(BuildingID::BLACKSMITH);
		}
		JsonNode serialized;
		JsonSerializer writer(nullptr, serialized);
		town->CGObjectInstance::serializeJson(writer);
		town->removeAllBuildings();
		town->forbiddenBuildings.clear();
		JsonDeserializer reader(nullptr, serialized);
		town->CGObjectInstance::serializeJson(reader);
		EXPECT_EQ(town->hasBuilt(BuildingID::DEFAULT), GetParam() < 2);
		JsonNode chances;
		chances.Vector().emplace_back(100);
		loaded->overrideGameSetting(EGameSettings::TOWNS_STARTING_DWELLING_CHANCES, chances);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.hero({6, 7, 0}, HeroTypeID(1), PlayerColor(1))
			.town({12, 12, 0}, FactionID::CASTLE, PlayerColor(0));
		startWithMap(std::move(builder));
	}
};

TEST_P(TownMapDefaultsRoundTripTest, InitializesDefaultsWithoutChangingAuthoredLayouts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	EXPECT_EQ(town->hasBuilt(BuildingID::FORT), GetParam() != 1);
	EXPECT_EQ(town->hasBuilt(BuildingID::TAVERN), GetParam() < 2);
	EXPECT_EQ(town->hasBuilt(BuildingID::DWELL_LVL_1), GetParam() < 2);
	EXPECT_EQ(town->hasBuilt(BuildingID::MAGES_GUILD_1), GetParam() == 3);
	EXPECT_EQ(town->forbiddenBuildings.contains(BuildingID::BLACKSMITH), GetParam() == 3);
}

INSTANTIATE_TEST_SUITE_P(DefaultAndAuthoredLayouts, TownMapDefaultsRoundTripTest,
	::testing::Values(0, 1, 2, 3));
}
