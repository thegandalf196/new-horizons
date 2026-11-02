/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/NewHorizonsNeutralCalibration.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <limits>

namespace
{
using namespace newHorizonsNeutralArmies::calibration;
constexpr auto option = EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION;

JsonNode curve()
{
	JsonNode result;
	result["version"].Integer() = 1;
	result["enabled"].Bool() = true;
	for(const auto [input, output] : {std::pair{1000, 1000}, std::pair{5000, 2000}, std::pair{10000, 4000}})
	{
		JsonNode point;
		point["original"].Integer() = input;
		point["fieldable"].Integer() = output;
		result["curve"].Vector().push_back(point);
	}
	return result;
}

JsonNode configured()
{
	JsonNode result;
	result["creatures"][SETTING_KEY] = curve();
	return result;
}
}

TEST(NewHorizonsNeutralCalibration, SmallBudgetsStayIntactAndLargeBudgetsRemainOrdered)
{
	const Rules rules(curve());
	int64_t previous = 0;
	for(int64_t value = 0; value <= 100000; ++value)
	{
		const auto actual = rules.budget(value);
		EXPECT_GE(actual, previous);
		EXPECT_LE(actual, value);
		EXPECT_LE(actual, 4000);
		if(value <= 1000)
			EXPECT_EQ(actual, value);
		previous = actual;
	}
	EXPECT_EQ(rules.budget(3000), 1500);
	EXPECT_EQ(rules.budget(std::numeric_limits<int64_t>::max()), 4000);
}

TEST(NewHorizonsNeutralCalibration, WholeBankGetsOneBudgetWithStableComposition)
{
	const Rules rules(curve());
	const auto counts = rules.counts({{100, 60}, {100, 60}, {100, 60}});
	EXPECT_EQ(counts, (std::vector<TQuantity>{22, 22, 22}));
	EXPECT_LE((counts[0] + counts[1] + counts[2]) * 60, rules.budget(18000));
	EXPECT_EQ(rules.counts({{0, 60}, {100, 60}}).front(), 0);
	EXPECT_EQ(rules.counts({{100, 8613}, {100, 8613}}), (std::vector<TQuantity>{1, 1}));
}

TEST(NewHorizonsNeutralCalibration, SingleStackRoundingNeverInflatesOrDeletesCreatures)
{
	const Rules rules(curve());
	for(const int value : {60, 1589, 8613})
	{
		int previous = 0;
		for(int count = 0; count < 10000; ++count)
		{
			const auto actual = rules.counts({{count, value}}).front();
			EXPECT_GE(actual, previous);
			EXPECT_LE(actual, count);
			EXPECT_EQ(actual > 0, count > 0);
			previous = actual;
		}
	}
}

TEST(NewHorizonsNeutralCalibration, DisabledContextsPreserveLegacyZeroValueStacks)
{
	const Rules disabled{JsonNode()};
	EXPECT_EQ(disabled.counts({{7, 0}}), (std::vector<TQuantity>{7}));
	EXPECT_EQ(disabled.budget(100000), 100000);
}

TEST(NewHorizonsNeutralCalibration, InvalidCaptureOrUnsupportedAdmissionRejects)
{
	auto invalid = curve();
	invalid["curve"].Vector()[1]["original"].Integer() = 500;
	EXPECT_THROW(Rules{invalid}, std::runtime_error);
	invalid = curve();
	invalid["curve"].Vector()[1]["fieldable"].Integer() = 6000;
	EXPECT_THROW(Rules{invalid}, std::runtime_error);
	EXPECT_THROW(validateOverrides(configured(), false), std::runtime_error);
	EXPECT_NO_THROW(validateOverrides(configured(), true));
}

TEST(NewHorizonsNeutralCalibration, CapturedCurveRoundTripsWithoutInstalledDefaults)
{
	GameSettings settings;
	settings.addOverride(option, curve());
	CMemorySerializer first;
	first.oser.version = first.iser.version = ESerializationVersion::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION;
	first.oser & settings;
	GameSettings restored;
	auto different = configured();
	different["creatures"][SETTING_KEY]["curve"].Vector()[2]["fieldable"].Integer() = 3000;
	restored.loadBase(different);
	first.iser & restored;
	EXPECT_EQ(restored.getValue(option), curve());
	CMemorySerializer second;
	second.oser.version = second.iser.version = ESerializationVersion::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION;
	second.oser & restored;
	GameSettings reread;
	second.iser & reread;
	EXPECT_EQ(reread.getValue(option), curve());
}

TEST(NewHorizonsNeutralCalibration, HistoricalAbsenceDisablesInstalledCalibration)
{
	for(const auto version : {ESerializationVersion::NEW_HORIZONS_SORCERY_REVISION, ESerializationVersion::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION})
	{
		CMemorySerializer memory;
		memory.oser.version = memory.iser.version = version;
		JsonNode empty;
		memory.oser & empty;
		GameSettings restored;
		restored.loadBase(configured());
		memory.iser & restored;
		EXPECT_FALSE(Rules(restored.getValue(option)).enabled());
	}
}

TEST(NewHorizonsNeutralCalibration, ReferenceCostOverflowRejectsBeforeCurveCapture)
{
	// Both growth and recruitment Gold fit signed32 fields; their52-week cost
	// does not fit signed64. Reject before performing the overflowing product.
	EXPECT_THROW(affordableValue({{52000000000LL, 1, 1000000000}}, 1000000), std::runtime_error);
	// Each member cost is representable; the third addition would overflow.
	EXPECT_THROW(affordableValue({{4000000000LL, 1, 1000000000},
		{4000000000LL, 1, 1000000000}, {4000000000LL, 1, 1000000000}}, 1000000), std::runtime_error);
	EXPECT_EQ(affordableValue({{7, 80, 60}, {5, 126, 100}}, 4000), 1190);
	EXPECT_EQ(affordableValue({{7, 80, 60}, {5, 126, 100}}, 460), 492);
	EXPECT_EQ(affordableValue({{std::numeric_limits<int64_t>::max(), 1, 1}}, 1), 1);
}
