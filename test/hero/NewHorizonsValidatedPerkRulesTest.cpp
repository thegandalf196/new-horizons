/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include <stdexcept>
#include <type_traits>

namespace
{
using namespace newHorizonsHeroes;
JsonNode canonicalRules()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
}
}

static_assert(!std::is_copy_constructible_v<ValidatedPerkRules>);
static_assert(!std::is_move_constructible_v<ValidatedPerkRules>);
static_assert(!std::is_constructible_v<ValidatedPerkRules, JsonNode &&>);
static_assert(!std::is_constructible_v<ValidatedPerkRules, const JsonNode &&>);

TEST(NewHorizonsValidatedPerkRules, EveryDefinitionMatchesTheStrictRawLookup)
{
	const auto rules = canonicalRules();
	const ValidatedPerkRules view(rules);
	for(const auto & [skill, row] : rules["skills"].Struct())
		for(const auto & perk : row["perks"].Vector())
			EXPECT_EQ(view.definition(skill, perk["id"].String()),
				perkDefinition(rules, skill, perk["id"].String()));
	EXPECT_FALSE(view.definition("missing:skill", "missing:perk"));
	EXPECT_FALSE(view.definition("new-horizons:offense", "missing:perk"));
}

TEST(NewHorizonsValidatedPerkRules, EmptyHistoricalRulesStillHaveNoDefinitions)
{
	const JsonNode absent;
	const ValidatedPerkRules missing(absent);
	EXPECT_FALSE(missing.definition("new-horizons:offense", "missing:perk"));
	auto empty = canonicalRules();
	empty.Struct().clear();
	const ValidatedPerkRules legacy(empty);
	EXPECT_FALSE(legacy.definition("new-horizons:offense", "missing:perk"));
}

TEST(NewHorizonsValidatedPerkRules, ReturnedDefinitionOwnsItsValuesAfterTheBorrowEnds)
{
	auto rules = canonicalRules();
	const auto & skill = rules["skills"].Struct().begin()->first;
	const auto perk = rules["skills"][skill]["perks"].Vector().front()["id"].String();
	std::optional<PerkDefinition> result;
	{
		const ValidatedPerkRules view(rules);
		result = view.definition(skill, perk);
	}
	ASSERT_TRUE(result);
	const auto expected = *result;
	rules.Struct().clear();
	EXPECT_EQ(*result, expected);
}

TEST(NewHorizonsValidatedPerkRules, ReplacementAtTheSameAddressMustPassFreshAdmission)
{
	auto rules = canonicalRules();
	{
		const ValidatedPerkRules first(rules);
		EXPECT_FALSE(first.definition("missing:skill", "missing:perk"));
	}
	// Keep schema/source hash and address unchanged; structural mutation matters.
	rules["skills"].Struct().erase(rules["skills"].Struct().begin());
	EXPECT_THROW((ValidatedPerkRules(rules)), std::runtime_error);
	rules = canonicalRules();
	EXPECT_NO_THROW((ValidatedPerkRules(rules)));
}

TEST(NewHorizonsValidatedPerkRules, MalformedUnrequestedPerkCannotHideBehindAValidLookup)
{
	auto rules = canonicalRules();
	rules["skills"].Struct().rbegin()->second["perks"].Vector().back()["requires"].String() = "unknown-tier";
	EXPECT_THROW((ValidatedPerkRules(rules)), std::runtime_error);
}

TEST(NewHorizonsValidatedPerkRules, CapturedPlannedStatusAndOptionalDetailsAreNotReinterpreted)
{
	auto rules = canonicalRules();
	const auto skill = rules["skills"].Struct().begin()->first;
	auto & row = rules["skills"][skill]["perks"].Vector().front();
	const auto perk = row["id"].String();
	row["effect"]["status"].String() = "planned";
	row.Struct().erase("details");
	const ValidatedPerkRules view(rules);
	const auto result = view.definition(skill, perk);
	ASSERT_TRUE(result);
	EXPECT_EQ(result, perkDefinition(rules, skill, perk));
	EXPECT_EQ(result->effect["status"].String(), "planned");
	EXPECT_TRUE(result->details.empty());
}
