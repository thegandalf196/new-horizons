/*
 * NewHorizonsSaveDirectoryTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../lib/VCMIDirs.h"
#include <cstdlib>
#include <fstream>
#include <optional>

#if defined(VCMI_XDG) && !defined(VCMI_ANDROID) && !defined(VCMI_PORTMASTER)
namespace
{
class ScopedEnvironment
{
	std::string name;
	std::optional<std::string> previous;
public:
	explicit ScopedEnvironment(const char * variable) : name(variable)
	{
		if(const char * value = std::getenv(variable))
			previous = value;
	}
	~ScopedEnvironment()
	{
		if(previous)
			setenv(name.c_str(), previous->c_str(), 1);
		else
			unsetenv(name.c_str());
	}
};

class NewHorizonsSaveDirectoryTest : public ::testing::Test
{
	ScopedEnvironment home{"HOME"};
	ScopedEnvironment data{"XDG_DATA_HOME"};
	ScopedEnvironment config{"XDG_CONFIG_HOME"};
	ScopedEnvironment cache{"XDG_CACHE_HOME"};
protected:
	boost::filesystem::path root;
	const IVCMIDirs * directories = nullptr;
	void SetUp() override
	{
		root = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("nh-save-path-%%%%-%%%%-%%%%");
		setenv("HOME", (root / "home").string().c_str(), 1);
		setenv("XDG_DATA_HOME", (root / "data").string().c_str(), 1);
		setenv("XDG_CONFIG_HOME", (root / "config").string().c_str(), 1);
		setenv("XDG_CACHE_HOME", (root / "cache").string().c_str(), 1);
		// Even first-time singleton initialization can write only this private fixture.
		directories = &VCMIDirs::get();
	}
	void TearDown() override
	{
		boost::filesystem::remove_all(root);
	}
};
}

TEST_F(NewHorizonsSaveDirectoryTest, ExplicitXdgRoutesOnlySavesToNewHorizons)
{
	EXPECT_EQ(directories->userSavePath(), root / "data" / "new-horizons" / "Saves");
	EXPECT_EQ(directories->userDataPath(), root / "data" / "vcmi");
	EXPECT_EQ(directories->userConfigPath(), root / "config" / "vcmi");
	EXPECT_EQ(directories->userCachePath(), root / "cache" / "vcmi");
	const auto oldSaves = root / "data" / "vcmi" / "Saves";
	boost::filesystem::create_directories(oldSaves);
	const auto legacySave = oldSaves / "retained.vcgm1";
	{
		std::ofstream output(legacySave.string(), std::ios::binary);
		output << "unchanged legacy save";
	}
	const auto before = boost::filesystem::file_size(legacySave);
	EXPECT_EQ(directories->userSavePath(), root / "data" / "new-horizons" / "Saves");
	EXPECT_TRUE(boost::filesystem::exists(legacySave));
	EXPECT_EQ(boost::filesystem::file_size(legacySave), before);
	std::ifstream input(legacySave.string(), std::ios::binary);
	EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "unchanged legacy save");
}

TEST_F(NewHorizonsSaveDirectoryTest, HomeFallbackKeepsEngineNamespacesUnchanged)
{
	unsetenv("XDG_DATA_HOME");
	unsetenv("XDG_CONFIG_HOME");
	unsetenv("XDG_CACHE_HOME");
	EXPECT_EQ(directories->userSavePath(), root / "home" / ".local" / "share" / "new-horizons" / "Saves");
	EXPECT_EQ(directories->userDataPath(), root / "home" / ".local" / "share" / "vcmi");
	EXPECT_EQ(directories->userConfigPath(), root / "home" / ".config" / "vcmi");
	EXPECT_EQ(directories->userCachePath(), root / "home" / ".cache" / "vcmi");
}

TEST_F(NewHorizonsSaveDirectoryTest, MissingHomeStillUsesNamedSaveDirectoryWithoutMigration)
{
	unsetenv("XDG_DATA_HOME");
	unsetenv("HOME");
	EXPECT_EQ(directories->userSavePath(), boost::filesystem::path(".") / "new-horizons" / "Saves");
	EXPECT_EQ(directories->userDataPath(), boost::filesystem::path("."));
}
#endif
