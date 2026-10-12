/*
 * CVcmiTestConfig.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CVcmiTestConfig.h"

#include "../lib/GameLibrary.h"
#include "../lib/modding/CModHandler.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

void CVcmiTestConfig::SetUp()
{
	const char * ordinaryExport = std::getenv("NH_EXPORT_HERO_PROC_ORDINARY_BOOTSTRAP");
	const bool useOrdinaryExport = ordinaryExport != nullptr;
	if(useOrdinaryExport)
	{
		const char * family = std::getenv("NH_EXPORT_HERO_PROC_FAMILY");
		const char * saveRoot = std::getenv("NH_EXPORT_HERO_PROC_SAVE_ROOT");
		if(std::string_view(ordinaryExport) != "1"
			|| ::testing::GTEST_FLAG(filter) != "NewHorizonsHeroProcFixtureExportTest.ExportLegalOfferOrderProcAdventureSave"
			|| !family || (std::string_view(family) != "attacker" && std::string_view(family) != "defender")
			|| !saveRoot || std::string_view(saveRoot).empty())
			throw std::runtime_error("Ordinary hero export bootstrap requires its exact isolated export invocation");
	}
	LIBRARY = new GameLibrary;
	// Default tests retain their independent TEST preset and fixture mod. Only
	// the explicitly selected player-save exporter uses the ordinary preset.
	LIBRARY->initializeFilesystem(false, /*useTestPreset*/ !useOrdinaryExport);
	if(useOrdinaryExport)
	{
		const auto & mods = LIBRARY->modh->getActiveMods();
		if(std::find(mods.begin(), mods.end(), "vcmi") == mods.end()
			|| std::find(mods.begin(), mods.end(), "new-horizons") == mods.end()
			|| std::any_of(mods.begin(), mods.end(), [](const auto & mod)
			{ return mod != "core" && mod != "vcmi" && mod != "new-horizons"; }))
			throw std::runtime_error("Ordinary hero export bootstrap requires only the shipping mods");
	}
	LIBRARY->initializeLibrary();
}

void CVcmiTestConfig::TearDown()
{
	std::cout << "Ending global test tear-down." << std::endl;
}

