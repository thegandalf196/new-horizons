/*
 * FilesystemTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"

#include "../../lib/filesystem/CFilesystemLoader.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/mapping/CMapService.h"
#include "../../lib/modding/CModHandler.h"
#include "../mock/TinyH3MBuilder.h"

#include <boost/filesystem/fstream.hpp>

namespace test
{

class FilesystemTest : public ::testing::Test
{
	boost::filesystem::path directory;

protected:
	const boost::filesystem::path & getDirectory() const
	{
		return directory;
	}

	void SetUp() override
	{
		directory = boost::filesystem::current_path()
			/ boost::filesystem::unique_path("vcmi-filesystem-%%%%-%%%%-%%%%-%%%%");
		ASSERT_TRUE(boost::filesystem::create_directory(directory));
	}

	void TearDown() override
	{
		boost::filesystem::remove_all(directory);
	}
};

TEST_F(FilesystemTest, RemovesResourceAndUpdatesIndex)
{
	CFilesystemLoader loader("Test/", getDirectory());
	const ResourcePath resource("Test/example.txt");

	ASSERT_TRUE(loader.createResource("Test/example.txt"));
	ASSERT_TRUE(loader.existsResource(resource));
	ASSERT_TRUE(boost::filesystem::exists(getDirectory() / "example.txt"));

	EXPECT_TRUE(loader.removeResource(resource));
	EXPECT_FALSE(loader.existsResource(resource));
	EXPECT_FALSE(boost::filesystem::exists(getDirectory() / "example.txt"));
}

TEST_F(FilesystemTest, GeneratedMapCreatedAfterInitializationIsRefreshable)
{
	const auto mapsDirectory = getDirectory() / "Maps";
	CFilesystemLoader loader("MAPS/", mapsDirectory);
	const ResourcePath resource("Maps/BattleOnlyMode.vmap");
	ASSERT_FALSE(boost::filesystem::exists(mapsDirectory));
	ASSERT_FALSE(loader.existsResource(resource));

	// Mimic saveMap's direct filesystem write, not loader.createResource.
	ASSERT_TRUE(boost::filesystem::create_directory(mapsDirectory));
	const auto mapPath = mapsDirectory / "BattleOnlyMode.vmap";
	boost::filesystem::ofstream map(mapPath);
	map << "synthetic map";
	map.close();
	ASSERT_FALSE(loader.existsResource(resource));
	loader.updateFilteredFiles([](const std::string &) { return true; });
	ASSERT_TRUE(loader.existsResource(resource));
	EXPECT_EQ(loader.getResourceName(resource), mapPath);
}

TEST_F(FilesystemTest, LateGeneratedUserMapHasCoreOriginAndLoadsHeader)
{
	const auto directoryName = boost::filesystem::unique_path("generated-map-%%%%-%%%%-%%%%");
	const auto mapsDirectory = VCMIDirs::get().userDataPath() / "Maps" / directoryName;
	const ResourcePath resource("Maps/" + directoryName.string() + "/BattleOnlyMode.h3m");
	ASSERT_FALSE(boost::filesystem::exists(mapsDirectory));
	ASSERT_FALSE(CResourceHandler::get("core")->existsResource(resource));
	ASSERT_FALSE(CResourceHandler::get("maps")->existsResource(resource));

	// Remove only this test's unique generated directory, including on assertion
	// failure, then drop its index entries. Installed/source maps are untouched.
	const auto cleanup = std::shared_ptr<void>(nullptr, [mapsDirectory](void *)
	{
		boost::filesystem::remove_all(mapsDirectory);
		CResourceHandler::get()->updateFilteredFiles([](const std::string &) { return true; });
	});
	ASSERT_TRUE(boost::filesystem::create_directories(mapsDirectory));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).name("Generated map origin regression");
	const auto bytes = builder.build();
	boost::filesystem::ofstream output(mapsDirectory / "BattleOnlyMode.h3m", std::ios::binary);
	output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	output.close();
	ASSERT_TRUE(output.good());
	ASSERT_FALSE(CResourceHandler::get("core")->existsResource(resource));

	// Match BattleOnlyModeTab: direct save, root refresh, then real header load.
	CResourceHandler::get()->updateFilteredFiles([](const std::string &) { return true; });
	ASSERT_TRUE(CResourceHandler::get("maps")->existsResource(resource));
	ASSERT_TRUE(CResourceHandler::get("core")->existsResource(resource));
	EXPECT_EQ(LIBRARY->modh->findResourceOrigin(resource), "core");
	CMapService service;
	const auto header = service.loadMapHeader(resource);
	ASSERT_NE(header, nullptr);
	EXPECT_EQ(header->width, 36);
	EXPECT_EQ(header->height, 36);
	// Header loading keeps map-local translations on the header; it does not
	// install them in the game's global active-map overlay.
	EXPECT_EQ(header->name.toString(header->texts.get()), "Generated map origin regression");
}

}
