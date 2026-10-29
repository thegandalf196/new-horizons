/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/NewHorizonsRecruitmentGrowth.h"
#include "../../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <limits>
#include <stdexcept>

namespace
{
constexpr auto option = EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT;
JsonNode growthSetting(int value)
{
	JsonNode node;
	node["creatures"][newHorizonsRecruitmentGrowth::SETTING_KEY].Integer() = value;
	return node;
}
}

TEST(NewHorizonsRecruitmentGrowth, OddCountsZeroIdentityAndAboveHundredUseFinalIntegerCeiling)
{
	using newHorizonsRecruitmentGrowth::scale;
	EXPECT_EQ(scale(0, 50), 0);
	EXPECT_EQ(scale(1, 50), 1);
	EXPECT_EQ(scale(3, 50), 2);
	EXPECT_EQ(scale(7, 50), 4);
	EXPECT_EQ(scale(10, 50), 5);
	EXPECT_EQ(scale(3, 150), 5);
	EXPECT_EQ(scale(7, 200), 14);
	EXPECT_EQ(scale(7, 1000), 70);
	EXPECT_EQ(scale(7, 0), 0);
	EXPECT_EQ(scale(7, 100), 7);
	EXPECT_EQ(scale(std::numeric_limits<int64_t>::max(), 100), std::numeric_limits<int64_t>::max());
	EXPECT_THROW(scale(std::numeric_limits<int64_t>::max(), 200), std::overflow_error);
	EXPECT_THROW(scale(-1, 50), std::runtime_error);
	EXPECT_THROW(scale(7, 1001), std::runtime_error);
}

TEST(NewHorizonsRecruitmentGrowth, BonusesAndGrailSubtotalAreRoundedOnlyAfterTheirCombinedOutput)
{
	GrowthInfo growth;
	EXPECT_EQ(growth.unscaledGrowth(), 0);
	EXPECT_EQ(growth.totalGrowth(), 0);
	growth.handicapPercentage = 100;
	growth.recruitmentGrowthPercentage = 50;
	growth.entries.emplace_back(1, "base");
	growth.entries.emplace_back(1, "castle");
	growth.entries.emplace_back(1, "flat bonus");
	EXPECT_EQ(growth.unscaledGrowth(), 3);
	EXPECT_EQ(growth.totalGrowth(), 2); // Three separately rounded halves would produce3.
	growth.entries.clear();
	growth.entries.emplace_back(7, "base");
	growth.entries.emplace_back(3, "ordinary Grail contribution");
	EXPECT_EQ(growth.totalGrowth(), 5);
	growth.recruitmentGrowthPercentage = 0;
	EXPECT_EQ(growth.totalGrowth(), 0);
}

TEST(NewHorizonsRecruitmentGrowth, CalendarOnlyScalesFreshIncrementAndPreservesExistingStockEvents)
{
	using newHorizonsRecruitmentGrowth::stockAfterWeek;
	EXPECT_EQ(stockAfterWeek(17, 3, 50, false, false), 19u);
	EXPECT_EQ(stockAfterWeek(17, 3, 50, true, false), 37u); //34old +ceil(6*50/100), not38.
	EXPECT_EQ(stockAfterWeek(17, 3, 0, false, false), 17u);
	EXPECT_EQ(stockAfterWeek(17, 3, 0, true, false), 34u);
	EXPECT_EQ(stockAfterWeek(17, 3, 1000, false, true), 8u);
	EXPECT_EQ(stockAfterWeek(17, 0, 50, false, false), 17u);
	EXPECT_THROW(stockAfterWeek(UINT32_MAX, 1, 200, false, false), std::overflow_error);
	EXPECT_THROW(stockAfterWeek(UINT32_MAX, 0, 50, true, false), std::overflow_error);
}

TEST(NewHorizonsRecruitmentGrowth, FreshFiftyCaptureSurvivesTwoLoadsWithDifferentInstalledDefaults)
{
	GameSettings fresh;
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(fresh), 100); // Unconfigured/historical identity.
	fresh.loadBase(growthSetting(50));
	fresh.addOverride(option, JsonNode(50));
	CMemorySerializer one;
	one.oser & fresh;
	GameSettings loaded;
	loaded.loadBase(growthSetting(200));
	one.iser & loaded;
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(loaded), 50);
	CMemorySerializer two;
	two.oser & loaded;
	GameSettings reloaded;
	two.iser & reloaded;
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(reloaded), 50);
}

TEST(NewHorizonsRecruitmentGrowth, HistoricalAbsenceMeansHundredEvenUnderNewInstalledFifty)
{
	for(const auto version : {ESerializationVersion::NEW_HORIZONS_GHOST_UPGRADE, ESerializationVersion::CURRENT})
	{
		CMemorySerializer wire;
		wire.oser.version = wire.iser.version = version;
		JsonNode absent;
		wire.oser & absent;
		GameSettings settings;
		settings.loadBase(growthSetting(50));
		settings.addOverride(option, JsonNode(0));
		wire.iser & settings;
		EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(settings), 100);
		CMemorySerializer again;
		again.oser & settings;
		GameSettings reread;
		reread.loadBase(growthSetting(50));
		again.iser & reread;
		EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(reread), 100);
	}
}

TEST(NewHorizonsRecruitmentGrowth, CapturedZeroIsDisabledAndCannotBecomeHistoricalAbsence)
{
	GameSettings settings;
	settings.loadBase(growthSetting(50));
	settings.addOverride(option, JsonNode(0));
	CMemorySerializer bytes;
	bytes.oser & settings;
	GameSettings loaded;
	bytes.iser & loaded;
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(loaded), 0);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::scale(7, newHorizonsRecruitmentGrowth::percentage(loaded)), 0);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_GHOST_UPGRADE;
	EXPECT_THROW(old.oser & settings, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST(NewHorizonsRecruitmentGrowth, MalformedAndUnsupportedReadsRejectBeforeReplacingCapturedSetting)
{
	for(const JsonNode & value : {JsonNode(), JsonNode(false), JsonNode("50"), JsonNode(50.0), JsonNode(-1), JsonNode(1001)})
	{
		GameSettings settings;
		settings.addOverride(option, JsonNode(50));
		JsonNode input;
		input["creatures"][newHorizonsRecruitmentGrowth::SETTING_KEY] = value;
		EXPECT_THROW(settings.addOverride(option, value), std::runtime_error);
		EXPECT_THROW(settings.loadOverrides(input), std::runtime_error);
		EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(settings), 50);
	}
	CMemorySerializer forged;
	forged.oser.version = forged.iser.version = ESerializationVersion::NEW_HORIZONS_GHOST_UPGRADE;
	auto input = growthSetting(50);
	forged.oser & input;
	GameSettings settings;
	settings.addOverride(option, JsonNode(200));
	EXPECT_THROW(forged.iser & settings, std::runtime_error);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(settings), 200);
}

TEST(NewHorizonsRecruitmentGrowth, NeutralAndRecruitmentCapturesRemainIndependent)
{
	GameSettings settings;
	settings.addOverride(option, JsonNode(50));
	settings.addOverride(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT, JsonNode(50));
	EXPECT_EQ(newHorizonsRecruitmentGrowth::scale(7, newHorizonsRecruitmentGrowth::percentage(settings)), 4);
	EXPECT_EQ(newHorizonsNeutralArmies::scaleCount(7, newHorizonsNeutralArmies::percentage(settings)), 4);
	settings.addOverride(option, JsonNode(0));
	EXPECT_EQ(newHorizonsNeutralArmies::scaleCount(7, newHorizonsNeutralArmies::percentage(settings)), 4);
}

TEST(NewHorizonsRecruitmentGrowth, AuthoredFreshDefaultsSelectHalfForBothIndependentSettings)
{
	const JsonNode config(JsonPath::builtin("config/gameConfig"));
	GameSettings settings;
	settings.loadBase(config["settings"]);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(settings), 50);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(settings), 50);
}

TEST(NewHorizonsRecruitmentGrowth, IdentityCaptureRemainsWritableToPreviousCodec)
{
	GameSettings settings;
	settings.addOverride(option, JsonNode(100));
	CMemorySerializer old;
	old.oser.version = old.iser.version = ESerializationVersion::NEW_HORIZONS_GHOST_UPGRADE;
	ASSERT_NO_THROW(old.oser & settings);
	GameSettings restored;
	ASSERT_NO_THROW(old.iser & restored);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(restored), 100);
}

TEST(NewHorizonsRecruitmentGrowth, LargeTuningRejectsStorageNarrowingInsteadOfWrapping)
{
	GameSettings settings;
	settings.addOverride(option, JsonNode(1000));
	EXPECT_THROW(newHorizonsRecruitmentGrowth::generatedAmount(UINT32_MAX, settings), std::overflow_error);
	GrowthInfo info;
	info.handicapPercentage = 100;
	info.recruitmentGrowthPercentage = 1000;
	info.entries.emplace_back(std::numeric_limits<int>::max() / 100, "large raw output");
	EXPECT_GT(info.totalGrowth(), 0);
	info.entries.clear();
	info.entries.emplace_back(std::numeric_limits<int>::max() / 2, "unsupported display output");
	EXPECT_THROW(info.totalGrowth(), std::overflow_error);
}
