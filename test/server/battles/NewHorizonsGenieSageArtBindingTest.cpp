/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsGenieSageFixture.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../client/windows/HeroSpecialtyPresentation.h"
using NewHorizonsGenieSageArtBindingTest = NewHorizonsGenieSageFixture;
namespace
{
void expectNativePair(const CGHeroInstance & hero, const char * stem)
{
	const auto beforeRules = hero.getPrimaryGrowthRules();
	const auto beforeBonuses = hero.getExportedBonusList().toJsonNode();
	const std::string artwork = "newHorizons/heroSpecialties/" + std::string(stem);
	ASSERT_EQ(hero.getSpecialtyPresentation().artwork, artwork);
	const auto card = heroSpecialtyPresentation(hero);
	EXPECT_EQ(card.animation().getOriginalName(), artwork + "/44");
	EXPECT_EQ(card.animation(true).getOriginalName(), artwork + "/32");
	EXPECT_EQ(card.frame(), 0u);
	EXPECT_EQ(hero.getPrimaryGrowthRules(), beforeRules);
	EXPECT_EQ(hero.getExportedBonusList().toJsonNode(), beforeBonuses);
}
}
TEST_F(NewHorizonsGenieSageArtBindingTest, CapturedWheelUsesSelectedNativePair)
{
	startAs("core:iona");
	EXPECT_TRUE(newHorizonsHeroes::hasWheelOfFortune(attackerSideHero));
	expectNativePair(*attackerSideHero, "IonaWheelOfFortune");
}
TEST_F(NewHorizonsGenieSageArtBindingTest, CapturedSageUsesSelectedNativePair)
{
	startAs("core:thane");
	EXPECT_TRUE(newHorizonsHeroes::hasSageOfTheAges(attackerSideHero));
	expectNativePair(*attackerSideHero, "ThaneSageOfTheAges");
}
TEST_F(NewHorizonsGenieSageArtBindingTest, HistoricalIonaRetainsGenieNativePair)
{
	historical = true;
	startAs("core:iona");
	EXPECT_FALSE(newHorizonsHeroes::hasWheelOfFortune(attackerSideHero));
	expectNativePair(*attackerSideHero, "IonaGenieLine");
}
TEST_F(NewHorizonsGenieSageArtBindingTest, HistoricalThaneRetainsGenieNativePair)
{
	historical = true;
	startAs("core:thane");
	EXPECT_FALSE(newHorizonsHeroes::hasSageOfTheAges(attackerSideHero));
	expectNativePair(*attackerSideHero, "ThaneGenieLine");
}
