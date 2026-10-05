/*
 * AcademyBuiltIconRuntimeTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 *
 * Loads the four runtime-generated Academy town-list icons at each SDL2 image
 * scale. SDL is constrained to its dummy video/audio drivers and software
 * renderer. ScreenHandler's existing constructor clear/present stays on that
 * dummy backend; the fixture has no event, input, or presentation loop.
 */
#include "../StdInc.h"

#include "../GameEngine.h"
#include "../CMT.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/logging/CBasicLogConfigurator.h"
#include "../../lib/modding/CModHandler.h"

#include "render/Canvas.h"
#include "render/IRenderHandler.h"
#include "render/IScreenHandler.h"

#include <SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

[[noreturn]] void handleFatalError(const std::string & message, bool)
{
	throw std::runtime_error("Unexpected VCMI fatal error in Academy icon fixture: " + message);
}

namespace
{
struct BuiltIcon
{
	const char * normal;
	const char * built;
	int nativeWidth;
	int nativeHeight;
};

constexpr std::array<BuiltIcon, 4> academyBuiltIcons{{
	{"NH_academy_fort_large_normal.png", "NH_academy_fort_large_built.png", 58, 64},
	{"NH_academy_fort_small_normal.png", "NH_academy_fort_small_built.png", 48, 32},
	{"NH_academy_village_large_normal.png", "NH_academy_village_large_built.png", 58, 64},
	{"NH_academy_village_small_normal.png", "NH_academy_village_small_built.png", 48, 32},
}};

void require(bool condition, const std::string & message)
{
	if(!condition)
		throw std::runtime_error(message);
}

void setRequiredEnvironment(const char * name, const std::filesystem::path & value)
{
	if(setenv(name, value.c_str(), 1) != 0)
		throw std::runtime_error(std::string("Could not set ") + name);
}

class IsolatedProfile
{
	std::filesystem::path root;

public:
	IsolatedProfile(const std::filesystem::path & originalData)
	{
		char pathTemplate[] = "/tmp/vcmi-academy-built-icons.XXXXXX";
		char * createdPath = mkdtemp(pathTemplate);
		require(createdPath != nullptr, "Could not create isolated runtime-test profile");
		root = createdPath;

		setRequiredEnvironment("XDG_DATA_HOME", root / "data");
		setRequiredEnvironment("XDG_CONFIG_HOME", root / "config");
		setRequiredEnvironment("XDG_CACHE_HOME", root / "cache");
		setRequiredEnvironment("SDL_VIDEODRIVER", "dummy");
		setRequiredEnvironment("SDL_AUDIODRIVER", "dummy");
		setRequiredEnvironment("SDL_RENDER_DRIVER", "software");

		const auto userConfig = root / "config" / "vcmi";
		std::filesystem::create_directories(userConfig);
		std::filesystem::create_directories(root / "data" / "vcmi");
		std::filesystem::create_directories(root / "cache" / "vcmi");
		std::filesystem::create_directory_symlink(originalData, root / "data" / "vcmi" / "Data");

		// This profile activates only the built-in content and New Horizons. It
		// prevents the renderer exercise from reading or rewriting the user's
		// normal mod configuration.
		std::ofstream mods(userConfig / "modSettings.json");
		require(static_cast<bool>(mods), "Could not create isolated New Horizons mod settings");
		mods << R"({"activePreset":"academy-icon-runtime","presets":{"academy-icon-runtime":{"mods":["vcmi","new-horizons"],"settings":{}}}})";
		require(static_cast<bool>(mods), "Could not write isolated New Horizons mod settings");
	}

	~IsolatedProfile()
	{
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}

	const std::filesystem::path & path() const { return root; }
};

void verifyDifferentPixels(const std::shared_ptr<IImage> & normal, const std::shared_ptr<IImage> & built,
		const BuiltIcon & icon)
{
	require(normal != nullptr, std::string("Could not load normal icon ") + icon.normal);
	require(built != nullptr, std::string("Could not generate built icon ") + icon.built);
	const Point size = normal->dimensions();
	require(size == built->dimensions(), std::string("Normal/built icon dimensions differ: ") + icon.built);
	require(size == Point(icon.nativeWidth, icon.nativeHeight), std::string("Unexpected native icon dimensions: ") + icon.built);

	Canvas normalCanvas(size, CanvasScalingPolicy::IGNORE);
	Canvas builtCanvas(size, CanvasScalingPolicy::IGNORE);
	normalCanvas.drawColor(Rect(Point(0, 0), size), ColorRGBA(0, 0, 0, 0));
	builtCanvas.drawColor(Rect(Point(0, 0), size), ColorRGBA(0, 0, 0, 0));
	normalCanvas.draw(normal, Point(0, 0));
	builtCanvas.draw(built, Point(0, 0));

	size_t changedPixels = 0;
	int minX = size.x;
	int minY = size.y;
	int maxX = -1;
	int maxY = -1;
	for(int y = 0; y < size.y; ++y)
	{
		for(int x = 0; x < size.x; ++x)
		{
			const Point pixel(x, y);
			if(normalCanvas.getPixel(pixel) != builtCanvas.getPixel(pixel))
			{
				++changedPixels;
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
		}
	}
	require(changedPixels > 0, std::string("Built badge did not change pixels for ") + icon.built);
	require(minX >= size.x - 17 && minY >= size.y - 16,
		std::string("Built badge escaped its expected lower-right region: ") + icon.built);
	require(changedPixels * 10 < static_cast<size_t>(size.x * size.y),
		std::string("Built icon changed more than ten percent of its source pixels: ") + icon.built);
	std::cout << "  " << icon.built << ": " << changedPixels << " badge-different pixels in "
		<< minX << ',' << minY << ".." << maxX << ',' << maxY << '\n';
}

void setUpscalingFilter(const char * name)
{
	Settings filter = settings.write["video"]["upscalingFilter"];
	filter->String() = name;
	Settings renderer = settings.write["video"]["driver"];
	renderer->String() = "software";
}

void runRuntimeRegression()
{
	const char * originalDataValue = std::getenv("NH_H3_DATA_DIR");
	require(originalDataValue && *originalDataValue,
		"Set NH_H3_DATA_DIR to the installed Heroes III Data directory before running this fixture");
	const auto originalData = std::filesystem::canonical(originalDataValue);
	require(std::filesystem::is_directory(originalData)
		&& std::filesystem::is_regular_file(originalData / "H3bitmap.lod")
		&& std::filesystem::is_regular_file(originalData / "H3sprite.lod"),
		"NH_H3_DATA_DIR must contain the original H3bitmap.lod and H3sprite.lod archives");

	IsolatedProfile profile(originalData);
	require(VCMIDirs::get().userDataPath() == profile.path() / "data" / "vcmi",
		"Runtime test did not select its isolated XDG data profile");
	require(VCMIDirs::get().userConfigPath() == profile.path() / "config" / "vcmi",
		"Runtime test did not select its isolated XDG config profile");

	CBasicLogConfigurator logging(VCMIDirs::get().userLogsPath() / "academy-built-icon-runtime.log", nullptr);
	logging.configureDefault();
	struct RuntimeCleanup
	{
		CBasicLogConfigurator & logging;
		~RuntimeCleanup()
		{
			ENGINE.reset();
			delete LIBRARY;
			LIBRARY = nullptr;
			CResourceHandler::destroy();
			logging.deconfigure();
		}
	} cleanup{logging};

	LIBRARY = new GameLibrary;
	LIBRARY->initializeFilesystem(false);
	LIBRARY->initializeLibrary();
	const auto & activeMods = LIBRARY->modh->getActiveMods();
	require(std::find(activeMods.begin(), activeMods.end(), "new-horizons") != activeMods.end(),
		"Isolated renderer test did not activate New Horizons");

	constexpr std::array<const char *, 4> upscalingFilters{{"none", "xbrz2", "xbrz3", "xbrz4"}};
	for(size_t factorIndex = 0; factorIndex < upscalingFilters.size(); ++factorIndex)
	{
		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
			throw std::runtime_error(std::string("SDL dummy initialization failed: ") + SDL_GetError());
		const char * activeVideoDriver = SDL_GetCurrentVideoDriver();
		require(activeVideoDriver && std::string(activeVideoDriver) == "dummy",
			"SDL did not use the dummy video driver before GameEngine construction");

		setUpscalingFilter(upscalingFilters[factorIndex]);
		ENGINE = std::make_unique<GameEngine>();
		const int expectedScale = factorIndex == 0 ? 1 : static_cast<int>(factorIndex + 1);
		require(ENGINE->screenHandler().getScalingFactor() == expectedScale,
			"ScreenHandler did not honor requested test scaling factor");

		auto & renderer = ENGINE->renderHandler();
		renderer.onLibraryLoadingFinished(LIBRARY);
		std::cout << "SDL dummy runtime scale " << expectedScale << "x\n";

		for(const BuiltIcon & icon : academyBuiltIcons)
		{
			auto normal = renderer.loadImage(ImagePath::builtin(icon.normal), EImageBlitMode::SIMPLE);
			auto built = renderer.loadImage(ImagePath::builtin(icon.built), EImageBlitMode::SIMPLE);
			verifyDifferentPixels(normal, built, icon);
		}

		// Destroying the backend releases SDL. The next iteration starts it again
		// under the same dummy environment and verifies the selected driver anew.
		ENGINE.reset();
	}

	std::cout << "Academy built-icon runtime regression PASS\n";
}
}

int main()
{
	const char * originalData = std::getenv("NH_H3_DATA_DIR");
	if(!originalData || !*originalData)
	{
		std::cout << "SKIP: set NH_H3_DATA_DIR to the installed Heroes III Data directory\n";
		return 77;
	}

	try
	{
		runRuntimeRegression();
	}
	catch(const std::exception & error)
	{
		std::cerr << "Academy built-icon runtime regression FAIL: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
