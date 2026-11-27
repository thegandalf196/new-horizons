/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../server/battles/NewHorizonsSoulHarvestFixture.h"
#include "../../lib/entities/hero/NewHorizonsNecropolisRetainedSpecialties.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

class NecropolisRetainedScaling : public TinyMapGameTest
{
protected:
	bool historical = false;
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
			for(const auto * key : {"bloodScent", "soulHarvest", "ancientOathsDiscount"})
				rules["revisedSpecialties"].Struct().erase(key);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void startHeroes()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:vokial")), PlayerColor(0))
			.hero({8, 5, 0}, HeroTypeID(HeroTypeID::decode("core:vidomina")), PlayerColor(0))
			.hero({11, 5, 0}, HeroTypeID(HeroTypeID::decode("core:nimbus")), PlayerColor(0));
		startWithMap(std::move(builder));
	}
};

TEST_F(NecropolisRetainedScaling, LevelOneGrowthCapAndHeroIdentity)
{
	ASSERT_NO_FATAL_FAILURE(startHeroes());
	auto * vokial = findHeroAt({5, 5, 0});
	auto * vidomina = findHeroAt({8, 5, 0});
	auto * seth = findHeroAt({11, 5, 0});
	ASSERT_TRUE(vokial && vidomina && seth);
	EXPECT_EQ(newHorizonsHeroes::bloodScentCapPercent(*vokial), 50);
	EXPECT_EQ(newHorizonsHeroes::soulHarvestPercent(vidomina), 100);
	EXPECT_EQ(newHorizonsHeroes::ancientOathsDiscountPercent(seth), 10);
	vokial->level = 2;
	EXPECT_EQ(newHorizonsHeroes::bloodScentCapPermille(*vokial), 525);
	vokial->level = vidomina->level = seth->level = 21;
	EXPECT_EQ(newHorizonsHeroes::bloodScentCapPercent(*vokial), 100);
	EXPECT_EQ(newHorizonsHeroes::soulHarvestPercent(vidomina), 160);
	EXPECT_EQ(newHorizonsHeroes::ancientOathsDiscountPercent(seth), 30);
	EXPECT_EQ(newHorizonsHeroes::soulHarvestPercent(vokial), 100);
	EXPECT_EQ(newHorizonsHeroes::ancientOathsDiscountPercent(vidomina), 0);
}

TEST_F(NecropolisRetainedScaling, AbsentCapturedRowsRetainHistoricalBenefits)
{
	historical = true;
	ASSERT_NO_FATAL_FAILURE(startHeroes());
	auto * vokial = findHeroAt({5, 5, 0});
	auto * vidomina = findHeroAt({8, 5, 0});
	auto * seth = findHeroAt({11, 5, 0});
	ASSERT_TRUE(vokial && vidomina && seth);
	vokial->level = vidomina->level = seth->level = 21;
	EXPECT_EQ(newHorizonsHeroes::bloodScentCapPercent(*vokial), 100);
	EXPECT_EQ(newHorizonsHeroes::soulHarvestPercent(vidomina), 100);
	EXPECT_EQ(newHorizonsHeroes::ancientOathsDiscountPercent(seth), 0);
}


TEST_F(NewHorizonsSoulHarvestTest, RevisedHarvestScalesTheWholeAttackThenRoundsDown)
{
	prepare(10, 100);
	attackerSideHero->level = 11;
	auto scaled = ghost->acquireState();
	ASSERT_TRUE(newHorizonsSoulHarvest::grant(*battle(), *scaled, 10));
	EXPECT_EQ(scaled->getCount(), 23);
	EXPECT_EQ(ghost->getCount(), 10);
	attackerSideHero->level = 2;
	auto rounded = ghost->acquireState();
	ASSERT_TRUE(newHorizonsSoulHarvest::grant(*battle(), *rounded, 3));
	EXPECT_EQ(rounded->getCount(), 13);
}
