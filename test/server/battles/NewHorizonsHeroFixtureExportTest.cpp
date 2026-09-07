/*
 * NewHorizonsHeroFixtureExportTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include <cstdlib>
#include <zlib.h>

class NewHorizonsHeroFixtureExportTest : public BattleTestFixture
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_F(NewHorizonsHeroFixtureExportTest, ExportOrdinaryKnightWithNearbyExperienceQuest)
{
	const auto * enabled = std::getenv("NH_EXPORT_HERO_FIXTURES");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned opt-in export requires NH_EXPORT_HERO_FIXTURES=1";
	if(!newHorizonsHeroes::usesRules(LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS)))
		GTEST_SKIP() << "Requires a separate actually activated future preset; no fixture rule overrides";
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsHeroes"));
	const std::string name = "NHHeroGrowthXP";
	const ArtifactID axe(ArtifactID::decode("core:centaurAxe"));
	using Builder = TinyH3M::TinyH3MBuilder;
	Builder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("Hero growth diagnostic, not a balanced scenario. Red human, Blue computer, Gold bonuses. "
			"Orrin starts at level1 with Pathfinding only, no authored primary overrides. "
			"Open hero development and save. Pick up the Centaur's Axe at18,12 (+2 Attack): inspect auto-equip, "
			"move it to backpack and re-equip; Base must not change. Visit the nearby Seer Hut east at21,10: its Orrin-only quest "
			"awards1000XP through normal dialogue, without taking troops. Complete the level-up choice, "
			"inspect actual gains and mana limit, save/reload. First growth should be3/4/1/2: "
			"no starting skill grants extra primary rolls. Do not compare class starts to old-save stats; "
			"old034 saves must remain legacy in the future client.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, HeroTypeID(0), PlayerColor(0))
		.heroExperience(0).heroSecondarySkills({{SecondarySkill::PATHFINDING, 1}})
		.heroGarrison({{creatureByName("dendroidGuard"), 150}, {creatureByName("grandElf"), 20}})
		.heroSpells({SpellID::HASTE, SpellID::MAGIC_ARROW})
		.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
		.seerHut({21, 10, 0}, Builder::missionHero(HeroTypeID(0)), Builder::rewardExperience(1000))
		.artifact({18, 12, 0}, axe);
	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	const auto header = parser.loadMapHeader(ResourcePath(name));
	ASSERT_NE(header, nullptr);
	ASSERT_EQ(header->version, EMapFormat::SOD);
	// Ordinary initialization: deliberately no mapLoaded/settings/rules override.
	startWithMap(builder);
	ASSERT_EQ(gameState()->getHeroDevelopmentRules(), canonical);
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto view = hero->getPrimaryGrowthView();
	ASSERT_TRUE(view);
	EXPECT_EQ(hero->level, 1);
	EXPECT_EQ(view->base, (std::array<int, 4>{15, 20, 5, 10}));
	EXPECT_EQ(view->modified, view->base);
	EXPECT_EQ(view->profile.growth, (std::array<int, 4>{3, 4, 1, 2}));
	EXPECT_TRUE(view->extraGrowth.empty());
	EXPECT_EQ(hero->mana, 10);
	EXPECT_EQ(hero->manaLimit(), 10);
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	ASSERT_NE(findObjectAt({21, 10, 0}), nullptr);
	const auto * pickup = findObjectAt({18, 12, 0});
	ASSERT_NE(pickup, nullptr);
	EXPECT_EQ(pickup->ID, Obj::ARTIFACT);
	EXPECT_EQ(pickup->subID, axe.getNum());
	ASSERT_EQ(builder.buildAndDump(name), expected);
	const auto path = VCMIDirs::get().userCachePath() / "testMaps" / (name + ".h3m");
	std::unique_ptr<gzFile_s, decltype(&gzclose)> input(gzopen(path.string().c_str(), "rb"), &gzclose);
	ASSERT_NE(input, nullptr);
	std::vector<uint8_t> actual;
	std::array<uint8_t, 4096> chunk;
	int count = 0;
	while((count = gzread(input.get(), chunk.data(), chunk.size())) > 0)
		actual.insert(actual.end(), chunk.begin(), chunk.begin() + count);
	ASSERT_EQ(count, 0);
	ASSERT_TRUE(gzeof(input.get()));
	ASSERT_EQ(gzclose(input.release()), Z_OK);
	ASSERT_EQ(actual, expected);
	MapServiceTinyH3M exported(actual, nullptr);
	ASSERT_NE(exported.loadMap(ResourcePath(name), gameState().get()), nullptr);
}
