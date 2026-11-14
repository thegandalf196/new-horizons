/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"

namespace
{
class SplitSkillOfferIntegrationTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
};
}

TEST_F(SplitSkillOfferIntegrationTest, RealHeroRetainsSeededWeightedDrawsWithinOrderedPools)
{
	TinyH3M::TinyH3MBuilder map(EMapFormat::SOD);
	map.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(0));
	startWithMap(std::move(map));
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(newHorizonsHeroes::usesSplitSkillOffers(hero->getPrimaryGrowthRules()));
	std::set<SecondarySkill> upgrades, fresh;
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
	{
		const SecondarySkill skill(index);
		const auto & rules = hero->getPrimaryGrowthRules();
		if(newHorizonsHeroes::isExcludedSkill(rules, skill)
			|| newHorizonsHeroes::skillOfferWeight(rules, skill).value_or(0) <= 0) continue;
		if(newHorizonsHeroes::isFactionSkill(rules, skill)
			&& !newHorizonsHeroes::isFactionSkillForFaction(rules, hero->getFactionID(), skill)) continue;
		const int rank = hero->getSecSkillLevel(skill);
		if(!hero->getPerkState().canAdvanceSkillNormally(skill.toSkill()->getJsonKey(), rank)) continue;
		if(rank > 0 && rank < MasteryLevel::EXPERT) upgrades.insert(skill);
		else if(rank == 0 && hero->canLearnSkill(skill)) fresh.insert(skill);
	}
	ASSERT_FALSE(upgrades.empty());
	ASSERT_FALSE(fresh.empty());
	GameRandomizer actual(*gameState()), reference(*gameState());
	actual.setSeed(2191);
	reference.setSeed(2191);
	for(int attempt = 0; attempt < 16; ++attempt)
	{
		const auto offered = actual.rollSecondarySkills(hero);
		const auto left = reference.rollSecondarySkillForLevelup(hero, upgrades);
		const auto right = reference.rollSecondarySkillForLevelup(hero, fresh);
		EXPECT_EQ(offered, (std::vector<SecondarySkill>{left, right}));
	}
}
