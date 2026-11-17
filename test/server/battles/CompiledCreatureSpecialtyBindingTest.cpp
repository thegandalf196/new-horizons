/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PreparedPilotFixture.h"
#include <tuple>
class CompiledCreatureSpecialtyBindingTest : public PreparedPilotFixture,
	public ::testing::WithParamInterface<std::tuple<std::tuple<const char *, const char *>, bool, bool>>
{
protected:
	void mapLoaded(CMap * map) override
	{
		PreparedPilotFixture::mapLoaded(map);
		if(!authored || !customValues) return;
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & value = heroes["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"][heroName]
			["mechanics"]["operations"][0]["value"];
		value.Integer() += 8;
		heroes.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroes);
	}
};
TEST_P(CompiledCreatureSpecialtyBindingTest, ActualHeroBindingPreservesCompletePreparedSourceAndVariantCaptures)
{
	heroName = std::get<0>(std::get<0>(GetParam()));
	creatureName = std::get<1>(std::get<0>(GetParam()));
	authored = std::get<1>(GetParam());
	customValues = std::get<2>(GetParam());
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	ASSERT_NE(attackerSideHero, nullptr);
	const auto * actual = attackerSideHero->getPreparedCreatureMechanics();
	const auto compiled = attackerSideHero->compiledDevelopment()->preparedCreature(attackerSideHero->getHeroTypeID());
	EXPECT_EQ(actual, compiled.get());
	if(!authored)
	{
		EXPECT_EQ(actual, nullptr);
		EXPECT_FALSE(nativeRules::preparePilotCreature(attackerSideHero->getPrimaryGrowthRules(), attackerSideHero->getHeroTypeID()));
		return;
	}
	ASSERT_NE(actual, nullptr);
	const auto expected = nativeRules::preparePilotCreature(attackerSideHero->getPrimaryGrowthRules(), attackerSideHero->getHeroTypeID());
	ASSERT_NE(expected, nullptr);
	EXPECT_EQ(actual->bonus().toJsonNode(), expected->bonus().toJsonNode());
	if(customValues)
	{
		const JsonNode original(JsonPath::builtin("config/newHorizonsHeroes"));
		EXPECT_EQ(actual->bonus().val, original["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"][heroName]
			["mechanics"]["operations"][0]["value"].Integer() + 8);
	}
	std::vector<std::shared_ptr<Bonus>> attached;
	for(const auto & bonus : attackerSideHero->getExportedBonusList())
		if(bonus->source == expected->bonus().source && bonus->sid == expected->bonus().sid
			&& bonus->stacking == expected->bonus().stacking) attached.push_back(bonus);
	ASSERT_EQ(attached.size(), 1u);
	EXPECT_EQ(attached.front()->toJsonNode(), expected->bonus().toJsonNode());
	EXPECT_EQ(attached.front()->valType, expected->bonus().valType);
	EXPECT_EQ(attached.front()->source, expected->bonus().source);
	EXPECT_EQ(attached.front()->sid, expected->bonus().sid);
	EXPECT_NE(attached.front().get(), &actual->bonus());
}
INSTANTIATE_TEST_SUITE_P(ActualCapturedProfiles, CompiledCreatureSpecialtyBindingTest,
	::testing::Combine(::testing::Values(std::tuple<const char *, const char *>{"core:fiona", "core:hellHound"},
	std::tuple<const char *, const char *>{"core:clancy", "core:unicorn"},
	std::tuple<const char *, const char *>{"core:pasis", "core:psychicElemental"}), ::testing::Bool(), ::testing::Bool()));
