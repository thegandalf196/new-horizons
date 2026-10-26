/*
 * NewHorizonsSkillPresentationTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../client/windows/NewHorizonsSkillPresentation.h"

namespace
{
constexpr std::string_view NECROMANCY = "new-horizons:necromancy";

JsonNode rulesWithRankText()
{
	JsonNode result;
	result["skills"][std::string(NECROMANCY)]["ranks"]["basic"]["description"].String() = "Raise 10%; Skeleton Leadership -50%.";
	result["skills"][std::string(NECROMANCY)]["ranks"]["advanced"]["description"].String() = "Raise 20%; Skeleton Leadership -60%.";
	result["skills"][std::string(NECROMANCY)]["ranks"]["expert"]["description"].String() = "Raise 30%; Skeleton Leadership -70%.";
	return result;
}
}

TEST(NewHorizonsSkillPresentation, UsesSuppliedRankTextWithoutChangingRules)
{
	auto rules = rulesWithRankText();
	const auto before = rules;
	EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(rules, NECROMANCY, 1, "installed"),
		"Raise 10%; Skeleton Leadership -50%.");
	EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(rules, NECROMANCY, 2, "installed"),
		"Raise 20%; Skeleton Leadership -60%.");
	EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(rules, NECROMANCY, 3, "installed"),
		"Raise 30%; Skeleton Leadership -70%.");
	EXPECT_EQ(rules, before);
}

TEST(NewHorizonsSkillPresentation, HistoricalSnapshotDoesNotBorrowInstalledDiscounts)
{
	auto historical = rulesWithRankText();
	historical["skills"][std::string(NECROMANCY)]["ranks"]["basic"]["description"].String() = "Historical raising only.";
	EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(historical, NECROMANCY, 1,
		"Installed Skeleton Leadership -50%."), "Historical raising only.");
	EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(JsonNode(), NECROMANCY, 1,
		"Ordinary raising description."), "Ordinary raising description.");
}

TEST(NewHorizonsSkillPresentation, UnknownMalformedAndUnrelatedEntriesKeepTranslatedFallback)
{
	auto rules = rulesWithRankText();
	for(const int rank : {0, -1, 4})
		EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(rules, NECROMANCY, rank, "fallback"), "fallback");
	EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(rules, "core:necromancy", 1, "legacy"), "legacy");
	rules["skills"]["new-horizons:offense"]["ranks"]["basic"]["description"].String() = "Registry offense.";
	EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(rules, "new-horizons:offense", 1, "Translated offense."),
		"Translated offense.");
	for(JsonNode malformed : {JsonNode(), JsonNode(7), JsonNode(std::string("invalid"))})
	{
		rules["skills"][std::string(NECROMANCY)]["ranks"]["basic"] = malformed;
		EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(rules, NECROMANCY, 1, "fallback"), "fallback");
	}
	rules["skills"][std::string(NECROMANCY)]["ranks"]["basic"] = JsonNode();
	rules["skills"][std::string(NECROMANCY)]["ranks"]["basic"]["description"].String().clear();
	EXPECT_EQ(newHorizonsSkillPresentation::rankDescription(rules, NECROMANCY, 1, "fallback"), "fallback");
}

TEST(NewHorizonsSkillPresentation, EncyclopediaSharesRankOrderAndHistoricalText)
{
	auto rules = rulesWithRankText();
	const auto description = newHorizonsSkillPresentation::allRankDescriptions(rules, NECROMANCY,
		{"installed basic", "installed advanced", "installed expert"});
	EXPECT_EQ(description, "Raise 10%; Skeleton Leadership -50%.\n\nRaise 20%; Skeleton Leadership -60%.\n\nRaise 30%; Skeleton Leadership -70%.");
	rules["skills"][std::string(NECROMANCY)]["ranks"]["advanced"]["description"].String() = "Historical advanced.";
	EXPECT_NE(newHorizonsSkillPresentation::allRankDescriptions(rules, NECROMANCY,
		{"basic", "advanced", "expert"}).find("Historical advanced."), std::string::npos);
	EXPECT_EQ(newHorizonsSkillPresentation::allRankDescriptions(JsonNode(), "core:necromancy",
		{"first", "", "third"}), "first\n\nthird");
}
