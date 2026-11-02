/*
 * NewHorizonsQuickStudyDefaultCaptureTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include <array>

namespace
{
constexpr std::array<const char *, 17> ordinarySpecialtyKeys = {
	"scorchingImpactDamageReductionPercent", "cruelOverseerDefenseReduction",
	"lanceFormationDefensePerHex", "lanceFormationMaximumDefense",
	"stormWingsDefenseIgnorePercent", "stormWingsMinimumFlightDistance",
	"consecratedTargetDefenseIgnorePercent", "featheredFeintPhysicalReductionIgnorePercent",
	"demonicSolidarityReductionPercent", "ironHideReductionPercent",
	"graveDustHealingReductionBasisPoints", "cinderReprisalDamagePercent",
	"emberbrandDamagePercent", "toxicStingChancePercent", "gnollHarriersDamagePercent",
	"prismaticVanguardPhysicalDamagePercent", "siegeSapperCreatureDefenseIgnorePercent"
};

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

void expectOptionalQuickStudy(const JsonNode & captured)
{
	const auto & policy = captured["quickStudyPolicy"];
	ASSERT_TRUE(policy.isStruct());
	EXPECT_EQ(policy["version"].Integer(), 1);
	EXPECT_EQ(policy["interval"].Integer(), 3);
}

class NewHorizonsQuickStudyDefaultCaptureTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
};
}

TEST_F(NewHorizonsQuickStudyDefaultCaptureTest, FreshWorldAndEveryMapHeroCaptureQuickStudyWithoutOrdinarySpecialties)
{
	ASSERT_EQ(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	const CreatureID troop(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{troop, 7}})
		.hero({8, 8, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{troop, 7}});
	ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder)));
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
