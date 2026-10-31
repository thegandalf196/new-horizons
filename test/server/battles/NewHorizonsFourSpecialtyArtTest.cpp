/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/NewHorizonsSpecialtyArtwork.h"
#include "../../../client/windows/HeroSpecialtyPresentation.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include <tuple>

namespace
{
using ArtCase = std::tuple<const char *, const char *, const char *, const char *>;
class NewHorizonsFourSpecialtyArtTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<ArtCase>
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
	const CHero & prototype() const
	{
		return *HeroTypeID(HeroTypeID::decode(std::get<0>(GetParam()))).toHeroType();
	}
	void expectSelected(const JsonNode & rules)
	{
		const auto & hero = prototype();
		const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(hero, rules, JsonNode(), std::nullopt);
		const std::string stem = "newHorizons/heroSpecialties/" + std::string(std::get<2>(GetParam()));
		ASSERT_EQ(metadata.artwork, stem);
		EXPECT_EQ(metadata.name, std::get<3>(GetParam()));
		const auto view = heroSpecialtyPresentation(hero, metadata);
		EXPECT_EQ(view.animation().getOriginalName(), stem + "/44");
		EXPECT_EQ(view.animation(true).getOriginalName(), stem + "/32");
		EXPECT_EQ(view.frame(), 0u);
		EXPECT_EQ(view.name, metadata.name);
		EXPECT_EQ(view.description, metadata.description);
	}
};
TEST_P(NewHorizonsFourSpecialtyArtTest, CurrentPolicySelectsExactNativePair)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	expectSelected(rules);
}
TEST_P(NewHorizonsFourSpecialtyArtTest, ZeroCapturedCoefficientKeepsSelectedPair)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][std::get<1>(GetParam())].Integer() = 0;
	expectSelected(rules);
}
TEST_P(NewHorizonsFourSpecialtyArtTest, MissingCapturedCoefficientRetainsHistoricalPresentation)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	rules["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().erase(std::get<1>(GetParam()));
	const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(prototype(), rules, JsonNode(), std::nullopt);
	rules["additionalSpecialties"].Struct().erase("ordinaryCreatureSpecialties");
	const auto historical = newHorizonsHeroes::resolveSpecialtyPresentation(prototype(), rules, JsonNode(), std::nullopt);
	EXPECT_EQ(metadata.artwork, historical.artwork);
	EXPECT_EQ(metadata.name, historical.name);
	EXPECT_EQ(metadata.description, historical.description);
	const auto view = heroSpecialtyPresentation(prototype(), metadata);
	const auto oldView = heroSpecialtyPresentation(prototype(), historical);
	EXPECT_EQ(view.animation().getOriginalName(), oldView.animation().getOriginalName());
	EXPECT_EQ(view.animation(true).getOriginalName(), oldView.animation(true).getOriginalName());
	EXPECT_EQ(view.frame(), oldView.frame());
}
TEST_P(NewHorizonsFourSpecialtyArtTest, AbsentConversionContextRetainsPrototypePresentation)
{
	const auto & hero = prototype();
	const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(hero, JsonNode(), JsonNode(), std::nullopt);
	EXPECT_FALSE(metadata.artwork);
	EXPECT_EQ(metadata.name, hero.getSpecialtyNameTranslated());
	const auto view = heroSpecialtyPresentation(hero, metadata);
	EXPECT_EQ(view.animation().getOriginalName(), "UN44");
	EXPECT_EQ(view.animation(true).getOriginalName(), "UN32");
	EXPECT_EQ(view.frame(), static_cast<size_t>(hero.imageIndex));
}
INSTANTIATE_TEST_SUITE_P(SelectedFour, NewHorizonsFourSpecialtyArtTest, ::testing::Values(
	ArtCase{"core:pasis", "prismaticScreenReductionPercent", "PasisPrismaticScreen", "Prismatic Screen"},
	ArtCase{"core:kalt", "tidalRenewalHealingHP", "KaltTidalRenewal", "Tidal Renewal"},
	ArtCase{"core:korbac", "evasiveFlightReductionPercent", "KorbacEvasiveFlight", "Evasive Flight"},
	ArtCase{"core:tyraxor", "ridersExploitDamagePercent", "TyraxorRidersExploit", "Rider's Exploit"}
));
}
