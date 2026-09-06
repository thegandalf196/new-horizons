/*
 * NewHorizonsModCompatibilityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/modding/ActiveModsInSaveList.h"
#include "../../lib/modding/ModDescription.h"
#include "../../lib/modding/ModIncompatibility.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
ModCompatibilityInfo installedHeader()
{
	ModCompatibilityInfo result;
	for(const auto & mod : LIBRARY->modh->getActiveMods())
		if(LIBRARY->modh->getModInfo(mod).affectsGameplay())
			result.emplace(mod, LIBRARY->modh->getModInfo(mod).getVerificationInfo());
	return result;
}

void readHeader(ModCompatibilityInfo entries)
{
	CMemorySerializer wire;
	std::vector<TModID> names;
	for(const auto & [name, info] : entries)
		names.push_back(name);
	wire.oser & names;
	for(const auto & name : names)
		wire.oser & entries.at(name);
	ActiveModsInSaveList incoming;
	wire.iser & incoming;
}
}

TEST(NewHorizonsModCompatibilityTest, MatchingInstalledHeaderLoads)
{
	EXPECT_NO_THROW(readHeader(installedHeader()));
}

TEST(NewHorizonsModCompatibilityTest, OtherExcessiveGameplayModsStillReject)
{
	auto header = installedHeader();
	ASSERT_EQ(header.erase("vcmi-test"), 1u);
	EXPECT_THROW(readHeader(header), ModIncompatibility);
}

TEST(NewHorizonsModCompatibilityTest, MissingRequiredGameplayModStillRejects)
{
	auto header = installedHeader();
	auto missing = LIBRARY->modh->getModInfo("vcmi-test").getVerificationInfo();
	missing.parent.clear();
	missing.impactsGameplay = true;
	missing.name = "Missing required native-test dependency";
	header.emplace("missing-required-native-test-dependency", missing);
	EXPECT_THROW(readHeader(header), ModIncompatibility);
}

TEST(NewHorizonsModCompatibilityTest, RequiredCuratedModuleCannotBeMissingOrDisabled)
{
	if(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Run this negative control in the unchanged baseline native preset";
	auto header = installedHeader();
	auto required = LIBRARY->modh->getModInfo("vcmi-test").getVerificationInfo();
	required.parent.clear();
	required.impactsGameplay = true;
	required.name = "Required New Horizons rules";
	header.emplace(GameConstants::NEW_HORIZONS_MOD_SCOPE, required);
	EXPECT_THROW(readHeader(header), ModIncompatibility);
}
