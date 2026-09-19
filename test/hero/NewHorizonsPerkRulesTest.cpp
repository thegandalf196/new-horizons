/*
 * NewHorizonsPerkRulesTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/json/JsonUtils.h"

#include <stdexcept>

TEST(NewHorizonsPerkRules, CanonicalRegistryValidatesAndLookupOwnsItsData)
{
	using namespace newHorizonsHeroes;
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsPerks", "canonical perk rules"));
	ASSERT_NO_THROW(validatePerkRules(rules));
	const auto skill = perkSkill(rules, "new-horizons:offense");
	ASSERT_TRUE(skill);
	EXPECT_EQ(skill->perks.size(), PERK_CANONICAL_POOL_SIZE);
	const auto perk = perkDefinition(rules, skill->id, skill->perks.front().id);
	ASSERT_TRUE(perk);
	EXPECT_EQ(perkRequiredRank(perk->requiredRank), 1);
	rules["skills"].Struct().clear();
	EXPECT_EQ(perk->id, skill->perks.front().id);
}

TEST(NewHorizonsPerkRules, EmptyRegistryIsLegacyAndUnknownLookupStaysEmpty)
{
	using namespace newHorizonsHeroes;
	EXPECT_NO_THROW(validatePerkRules(JsonNode()));
	EXPECT_NO_THROW(validatePerkRules(JsonNode(JsonMap{})));
	EXPECT_FALSE(perkSkill(JsonNode(), "new-horizons:anything"));
	EXPECT_FALSE(perkDefinition(JsonNode(), "new-horizons:anything", "new-horizons:anything.perk"));
}

TEST(NewHorizonsPerkRules, RuntimeRejectsIdentityCardinalityRankAndEffectForgery)
{
	using namespace newHorizonsHeroes;
	const JsonNode original(JsonPath::builtin("config/newHorizonsPerks"));
	std::vector<JsonNode> invalid(10, original);
	invalid[0]["skills"]["new-horizons:offense"]["id"].String() = "new-horizons:wrong";
	invalid[1]["skills"]["new-horizons:offense"]["perks"].Vector().pop_back();
	invalid[2]["skills"]["new-horizons:offense"]["perks"].Vector()[0]["requires"].String() = "master";
	invalid[3]["skills"]["new-horizons:offense"]["perks"].Vector()[0]["effect"]["status"].String() = "implemented";
	invalid[4]["skills"]["new-horizons:offense"]["perks"].Vector()[1]["id"] =
		invalid[4]["skills"]["new-horizons:offense"]["perks"].Vector()[0]["id"];
	invalid[5]["unexpected"].Bool() = true;
	invalid[6]["maxPerksPerSkill"].Integer() = 4;
	invalid[7]["skills"]["new-horizons:offense"]["perks"].Vector()[0]["id"].String() =
		"new-horizons:defense.borrowedPerk";
	invalid[8]["skills"]["new-horizons:offense"]["domain"].String() = "Anything";
	invalid[9]["skills"]["new-horizons:offense"]["availability"].String() = "Everyone sometimes";
	for(const auto & rules : invalid)
		EXPECT_THROW(validatePerkRules(rules), std::runtime_error);
}

TEST(NewHorizonsPerkRules, SettingsSchemaAcceptsTheActualRegistry)
{
	JsonNode settings;
	settings["heroes"]["newHorizonsPerks"] = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	EXPECT_TRUE(JsonUtils::validate(settings, "vcmi:gameSettings", "perk settings wrapper"));
}

TEST(NewHorizonsPerkRules, EveryCanonicalSkillResolvesToItsExactRegisteredEntity)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	for(const auto & [skillId, definition] : rules["skills"].Struct())
	{
		SCOPED_TRACE(skillId);
		const SecondarySkill decoded(SecondarySkill::decode(skillId));
		ASSERT_TRUE(decoded.hasValue());
		const auto * skill = LIBRARY->skills()->getById(decoded);
		ASSERT_NE(skill, nullptr);
		EXPECT_EQ(skill->getJsonKey(), skillId);
	}
}

TEST(NewHorizonsPerkRules, MissingOptionalSettingRemainsLegacyInsteadOfAsserting)
{
	GameSettings settings;
	EXPECT_TRUE(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS).isNull());
}
