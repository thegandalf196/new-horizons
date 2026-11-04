/*
 * NewHorizonsQuickStudyDefaultCaptureTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include <array>
#include <utility>

namespace
{
constexpr auto & ordinarySpecialtyKeys = historicalHeroRulesTest::extendedOrdinaryKeys;

bool hasOrdinarySpecialties(const JsonNode & captured)
{
	const auto & policy = captured["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	for(const auto * key : ordinarySpecialtyKeys)
		if(policy.Struct().contains(key))
			return true;
	return false;
}

void expectOrdinarySpecialtiesAbsent(const JsonNode & captured)
{
	ASSERT_TRUE(captured["additionalSpecialties"]["ordinaryCreatureSpecialties"].isStruct());
	EXPECT_FALSE(hasOrdinarySpecialties(captured));
}

void expectActivatedOrdinarySpecialties(const JsonNode & captured)
{
	const auto & policy = captured["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	ASSERT_TRUE(policy.isStruct());
	constexpr std::array<std::pair<const char *, int>, 16> expected = {{
		{"scorchingImpactDamageReductionPercent", 20}, {"cruelOverseerDefenseReduction", 3},
		{"lanceFormationDefensePerHex", 1}, {"lanceFormationMaximumDefense", 5},
		{"stormWingsDefenseIgnorePercent", 25}, {"stormWingsMinimumFlightDistance", 4},
		{"consecratedTargetDefenseIgnorePercent", 25}, {"featheredFeintPhysicalReductionIgnorePercent", 20},
		{"demonicSolidarityReductionPercent", 20}, {"ironHideReductionPercent", 30},
		{"graveDustHealingReductionBasisPoints", 5000}, {"cinderReprisalDamagePercent", 20},
		{"emberbrandDamagePercent", 20}, {"toxicStingChancePercent", 20},
		{"gnollHarriersDamagePercent", 20}, {"prismaticVanguardPhysicalDamagePercent", 20}
	}};
	for(const auto & [key, value] : expected)
	{
		SCOPED_TRACE(key);
		ASSERT_TRUE(policy.Struct().contains(key));
		EXPECT_EQ(policy[key].Integer(), value);
	}
	EXPECT_FALSE(policy.Struct().contains("siegeSapperCreatureDefenseIgnorePercent"));
}

void expectOptionalQuickStudy(const JsonNode & captured)
{
	const auto & policy = captured["connoisseurPolicy"];
	ASSERT_TRUE(policy.isStruct());
	EXPECT_EQ(policy["version"].Integer(), 1);
	EXPECT_FALSE(policy.Struct().contains("interval"));
}

class NewHorizonsQuickStudyDefaultCaptureTest : public TinyMapGameTest
{
protected:
	bool preExtendedCapture = false;
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		if(!preExtendedCapture)
			return;
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		historicalHeroRulesTest::omitPost1152OrdinarySpecialties(rules);
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void startTwoHeroes()
	{
		const CreatureID troop(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{troop, 7}})
			.hero({8, 8, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{troop, 7}});
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder)));
	}
};
}

TEST_F(NewHorizonsQuickStudyDefaultCaptureTest, FreshWorldAndEveryMapHeroCaptureQuickStudyWithoutOrdinarySpecialties)
{
	ASSERT_EQ(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	// Preserve the original absence oracle with an explicitly authored old policy.
	preExtendedCapture = true;
	ASSERT_NO_FATAL_FAILURE(startTwoHeroes());
	ASSERT_NO_FATAL_FAILURE(expectOrdinarySpecialtiesAbsent(gameState()->getHeroDevelopmentRules()));
	ASSERT_NO_FATAL_FAILURE(expectOptionalQuickStudy(gameState()->getHeroPerkRules()));
	const auto & heroes = gameState()->getMap().getHeroesOnMap();
	ASSERT_EQ(heroes.size(), 2u);
	for(const auto id : heroes)
	{
		SCOPED_TRACE(id.getNum());
		const auto * hero = gameState()->getHero(id);
		ASSERT_NE(hero, nullptr);
		ASSERT_NO_FATAL_FAILURE(expectOrdinarySpecialtiesAbsent(hero->getPrimaryGrowthRules()));
		ASSERT_NO_FATAL_FAILURE(expectOptionalQuickStudy(hero->getPerkState().rules));
	}
}

TEST_F(NewHorizonsQuickStudyDefaultCaptureTest, FreshWorldAndEveryMapHeroCaptureQuickStudyAndSixteenOrdinaryCoefficients)
{
	ASSERT_EQ(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	ASSERT_NO_FATAL_FAILURE(startTwoHeroes());
	ASSERT_NO_FATAL_FAILURE(expectActivatedOrdinarySpecialties(gameState()->getHeroDevelopmentRules()));
	ASSERT_NO_FATAL_FAILURE(expectOptionalQuickStudy(gameState()->getHeroPerkRules()));
	const auto & heroes = gameState()->getMap().getHeroesOnMap();
	ASSERT_EQ(heroes.size(), 2u);
	for(const auto id : heroes)
	{
		SCOPED_TRACE(id.getNum());
		const auto * hero = gameState()->getHero(id);
		ASSERT_NE(hero, nullptr);
		ASSERT_NO_FATAL_FAILURE(expectActivatedOrdinarySpecialties(hero->getPrimaryGrowthRules()));
		ASSERT_NO_FATAL_FAILURE(expectOptionalQuickStudy(hero->getPerkState().rules));
	}
}

TEST(NewHorizonsQuickStudyDefaultCapturePolicy, EveryNestedOrdinaryFieldIsDetectedEvenWhenZero)
{
	JsonNode captured;
	captured["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct();
	EXPECT_FALSE(hasOrdinarySpecialties(captured));
	for(const auto * key : ordinarySpecialtyKeys)
	{
		SCOPED_TRACE(key);
		auto present = captured;
		present["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = 0;
		EXPECT_TRUE(hasOrdinarySpecialties(present));
	}
}
