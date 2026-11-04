/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/json/JsonValidator.h"
#include "../../lib/mapObjects/NewHorizonsNeutralCalibration.h"
#include "../../lib/mapObjects/NewHorizonsRecruitmentGrowth.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/filesystem/ResourcePath.h"
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

JsonNode generation()
{
	const std::string text = R"({"version":1,"enabled":true,
		"stages":[{"heroLevel":1,"weeks":1,"unlockedLines":3,"troopGold":4000}],
		"unlockWeeks":[1,1,1,2,2,3,4]})";
	return JsonNode(text.data(), text.size(), "neutral calibration schema fixture");
}

std::string nativeSchemaErrors(JsonNode rules)
{
	// Core settings intentionally skip generic required-key checks. Presence
	// exclusions must therefore work without negating a required-key schema.
	rules.setModScope("core");
	return JsonValidator().check("vcmi:newHorizonsNeutralCalibration", rules);
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

TEST(NewHorizonsNeutralCalibration, NativeSchemaAcceptsCoreGenerationCaptureAndDisabledForms)
{
	JsonNode emptyObject;
	emptyObject.Struct();
	for(const auto & rules : std::vector<JsonNode>{JsonNode(), emptyObject, disabledSnapshot(), curve(), generation()})
	{
		const auto errors = nativeSchemaErrors(rules);
		EXPECT_TRUE(errors.empty()) << errors;
		EXPECT_NO_THROW(validate(rules));
	}
}

TEST(NewHorizonsNeutralCalibration, NativeSchemaRejectsMixedFormsEvenForNullCoreProperties)
{
	for(const char * key : {"stages", "unlockWeeks"})
	{
		auto captured = curve();
		captured[key] = JsonNode();
		EXPECT_FALSE(nativeSchemaErrors(captured).empty());
		EXPECT_THROW(validate(captured), std::runtime_error);
	}
	auto generated = generation();
	generated["curve"] = JsonNode();
	EXPECT_FALSE(nativeSchemaErrors(generated).empty());
	EXPECT_THROW(validate(generated), std::runtime_error);
}

TEST(NewHorizonsNeutralCalibration, NativeSchemaRejectsNonObjectScalarsAndArrays)
{
	JsonNode array;
	array.Vector().emplace_back(1);
	for(const auto & rules : std::vector<JsonNode>{JsonNode(1), JsonNode(true), JsonNode("invalid"), array})
	{
		EXPECT_FALSE(nativeSchemaErrors(rules).empty());
		EXPECT_THROW(validate(rules), std::runtime_error);
	}
}

namespace
{
void configureSingleKnightReference(GameSettings & settings, int recruitmentPercent)
{
	JsonNode capabilities(JsonPath::builtin("config/newHorizonsCapabilities"));
	const auto knight = capabilities["classProfiles"]["core:knight"];
	capabilities["classProfiles"].Struct().clear();
	capabilities["classProfiles"]["core:knight"] = knight;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, capabilities);
	settings.addOverride(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
		JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
	auto definition = generation();
	definition["stages"].Vector().front()["unlockedLines"].Integer() = 1;
	settings.addOverride(option, definition);
	settings.addOverride(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT, JsonNode(recruitmentPercent));
}

void expectCurrentPikemanReference()
{
	const auto * pikeman = CreatureID(CreatureID::decode("core:pikeman")).toCreature();
	ASSERT_NE(pikeman, nullptr);
	ASSERT_EQ(pikeman->getGrowth(), 14);
	ASSERT_EQ(pikeman->getAIValue(), 80);
	ASSERT_EQ(pikeman->getFullRecruitCost()[EGameResID::GOLD], 60);
}
}

TEST(NewHorizonsNeutralCalibration, ZeroRecruitmentUsesOnlyANonzeroReferenceAndKeepsActualGrowthZero)
{
	ASSERT_NO_FATAL_FAILURE(expectCurrentPikemanReference());
	GameSettings zero;
	GameSettings identity;
	configureSingleKnightReference(zero, 0);
	configureSingleKnightReference(identity, 100);
	const auto captured = capture(zero);
	EXPECT_NO_THROW(validate(captured));
	ASSERT_TRUE(Rules(captured).enabled());
	EXPECT_EQ(capture(zero), captured);
	EXPECT_EQ(captured, capture(identity));
	const std::string expected = R"({"version":1,"enabled":true,"curve":[{"original":1120,"fieldable":1120}]})";
	EXPECT_EQ(captured, JsonNode(expected.data(), expected.size(), "unscaled reference snapshot"));
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(zero), 0);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::scale(7, newHorizonsRecruitmentGrowth::percentage(zero)), 0);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::stockAfterWeek(2, 7, 0, false, false), 2u);
}

TEST(NewHorizonsNeutralCalibration, PositiveRecruitmentKeepsTheExactFiftyPercentReferenceSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(expectCurrentPikemanReference());
	GameSettings half;
	configureSingleKnightReference(half, 50);
	const auto captured = capture(half);
	// One week: original14 Pikemen, scaled7; Gold4000 and Leadership1025 do not bind.
	// At AI80 each, the unchanged pre-fix reference is1120 ->560 with identity knee560.
	const std::string expected = R"({"version":1,"enabled":true,"curve":[{"original":560,"fieldable":560},{"original":1120,"fieldable":560}]})";
	EXPECT_EQ(captured, JsonNode(expected.data(), expected.size(), "fifty percent reference snapshot"));
	EXPECT_EQ(capture(half), captured);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(half), 50);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::scale(7, 50), 4);
}
