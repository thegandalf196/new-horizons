/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../HistoricalFactionPerkRules.h"
#include "../../lib/GameSettings.h"
#include <gtest/gtest.h>

namespace
{
bool factionSkill(const std::string & id)
{
	return id == "new-horizons:metamagic" || id == "new-horizons:divineMandate"
		|| id == "new-horizons:demonicGating";
}
void expectHistoricalRegistry(const JsonNode & result, const JsonNode & installed)
{
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy"})
		EXPECT_FALSE(result.Struct().contains(key));
	for(const auto & [id, skill] : installed["skills"].Struct())
		if(!factionSkill(id))
			EXPECT_EQ(result["skills"][id], skill) << id;
}
}

TEST(HistoricalFactionPerkRules, FullRegistryOverrideCannotMergeNewPoliciesBack)
{
	auto installed = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy"})
		installed[key]["version"].Integer() = 2;
	JsonNode base(JsonPath::builtin("config/newHorizonsCombat"));
	base["heroes"]["newHorizonsPerks"] = installed;
	GameSettings settings;
	settings.loadBase(base);
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy"})
		ASSERT_TRUE(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS).Struct().contains(key));

	const auto historical = historicalFactionPerkRules(installed);
	ASSERT_TRUE(historical.getOverrideFlag());
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, historical);
	expectHistoricalRegistry(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), installed);
	for(const auto & [key, value] : installed.Struct())
		if(key != "skills" && key != "metamagicPolicy" && key != "divineMandatePolicy" && key != "demonicGatingPolicy")
			EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS)[key], value) << key;
}

TEST(HistoricalFactionPerkRules, NoArgumentOverloadUsesSameCompleteHistoricalRegistry)
{
	const JsonNode installed(JsonPath::builtin("config/newHorizonsPerks"));
	const auto result = historicalFactionPerkRules();
	EXPECT_TRUE(result.getOverrideFlag());
	expectHistoricalRegistry(result, installed);
	EXPECT_EQ(result, historicalFactionPerkRules(installed));
}
