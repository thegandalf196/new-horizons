/*
 * NewHorizonsNeutralArmySizeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <limits>
#include <stdexcept>

namespace
{
constexpr auto option = EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT;
JsonNode configured(int percent)
{
	JsonNode result;
	result["creatures"][newHorizonsNeutralArmies::SETTING_KEY].Integer() = percent;
	return result;
}
ESerializationVersion oldVersion()
{
	return ESerializationVersion::NEW_HORIZONS_SORCERY_REVISION;
}
}

TEST(NewHorizonsNeutralArmySize, IntegerCeilingPreservesZeroSingletonsAndMaximumCount)
{
	using newHorizonsNeutralArmies::scaleCount;
	for(const int count : {0, 1, 2, 3, 6, 7, 43, std::numeric_limits<int>::max()})
	{
		EXPECT_EQ(scaleCount(count, 100), count);
		EXPECT_EQ(scaleCount(count, 50), count / 2 + count % 2);
	}
	EXPECT_EQ(scaleCount(7, 50) + 6 * scaleCount(6, 50), 22);
	EXPECT_EQ(7 * scaleCount(1, 50), 7);
	EXPECT_EQ(scaleCount(100, 1), 1);
	EXPECT_THROW(scaleCount(-1, 50), std::runtime_error);
	for(const int invalid : {0, -1, 101, std::numeric_limits<int>::max()})
		EXPECT_THROW(scaleCount(7, invalid), std::runtime_error);
}

TEST(NewHorizonsNeutralArmySize, FreshOverrideInheritsUntilExplicitCaptureAndRoundTrips)
{
	GameSettings settings;
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(settings), 100);
	settings.loadBase(configured(50));
	settings.loadOverrides(JsonNode());
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(settings), 50);
	settings.addOverride(option, JsonNode(50));
	CMemorySerializer memory;
	memory.oser & settings;
	GameSettings restored;
	restored.loadBase(configured(100));
	memory.iser & restored;
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(restored), 50);
	CMemorySerializer second;
	second.oser & restored;
	GameSettings reread;
	second.iser & reread;
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(reread), 50);
}

TEST(NewHorizonsNeutralArmySize, HistoricalAbsenceOverridesInstalledHalfAndResavesAsIdentity)
{
	for(const auto version : {oldVersion(), ESerializationVersion::CURRENT})
	{
		CMemorySerializer memory;
		memory.oser.version = memory.iser.version = version;
		JsonNode absent;
		memory.oser & absent;
		GameSettings restored;
		restored.loadBase(configured(50));
		restored.addOverride(option, JsonNode(25));
		memory.iser & restored;
		EXPECT_EQ(newHorizonsNeutralArmies::percentage(restored), 100);
		CMemorySerializer second;
		second.oser & restored;
		GameSettings reread;
		reread.loadBase(configured(50));
		second.iser & reread;
		EXPECT_EQ(newHorizonsNeutralArmies::percentage(reread), 100);
	}
}

TEST(NewHorizonsNeutralArmySize, MeaningfulOldWritesAndForgedOldReadsRejectWithoutMutation)
{
	GameSettings settings;
	settings.addOverride(option, JsonNode(50));
	CMemorySerializer oldWriter;
	oldWriter.oser.version = oldVersion();
	EXPECT_THROW(oldWriter.oser & settings, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
	CMemorySerializer forged;
	forged.oser.version = forged.iser.version = oldVersion();
	auto raw = configured(50);
	forged.oser & raw;
	GameSettings restored;
	EXPECT_THROW(forged.iser & restored, std::runtime_error);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(restored), 100);
	settings.addOverride(option, JsonNode(100));
	CMemorySerializer identity;
	identity.oser.version = identity.iser.version = oldVersion();
	EXPECT_NO_THROW(identity.oser & settings);
	EXPECT_NO_THROW(identity.iser & restored);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(restored), 100);
}

TEST(NewHorizonsNeutralArmySize, AllAdmissionPathsRejectMalformedValuesBeforeReplacingCapture)
{
	std::vector<JsonNode> invalid = {JsonNode(), JsonNode(false), JsonNode("50"), JsonNode(0),
		JsonNode(-1), JsonNode(101), JsonNode(50.0), JsonNode(50.5)};
	for(const auto & value : invalid)
	{
		GameSettings settings;
		settings.addOverride(option, JsonNode(50));
		JsonNode input;
		input["creatures"][newHorizonsNeutralArmies::SETTING_KEY] = value;
		EXPECT_THROW(settings.loadBase(input), std::runtime_error);
		EXPECT_THROW(settings.addOverride(option, value), std::runtime_error);
		EXPECT_THROW(settings.loadOverrides(input), std::runtime_error);
		EXPECT_EQ(newHorizonsNeutralArmies::percentage(settings), 50);
		CMemorySerializer memory;
		memory.oser & input;
		EXPECT_THROW(memory.iser & settings, std::runtime_error);
		EXPECT_EQ(newHorizonsNeutralArmies::percentage(settings), 50);
	}
}
