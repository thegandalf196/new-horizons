/*
 * NewHorizonsMasteryFixtureExportTest.cpp, part of VCMI engine
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
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/NewHorizonsMasteryRules.h"
#include <cstdlib>
#include <zlib.h>

class NewHorizonsMasteryFixtureExportTest : public BattleTestFixture, public ::testing::WithParamInterface<bool>
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_P(NewHorizonsMasteryFixtureExportTest, ExportOrdinaryExperienceChoicesAndComputerEncounter)
{
	const auto * enabled = std::getenv("NH_EXPORT_MASTERY_FIXTURES");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned opt-in export requires NH_EXPORT_MASTERY_FIXTURES=1";
	ASSERT_TRUE(newHorizonsHeroes::usesRules(LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES)))
		<< "Requires actual private mastery activation; no rule/save injection";
	const bool newlyExpert = GetParam();
	const std::string name = newlyExpert ? "NHMasteryNewlyExpert" : "NHMasteryExpertChoices";
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsMasteries"));
	const auto pikeman = creatureByName("core:pikeman");
	const auto archer = creatureByName("core:archer");
	using Builder = TinyH3M::TinyH3MBuilder;
	Builder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description(newlyExpert
			? "Mastery boundary diagnostic, not a balanced scenario. Red human, Blue computer, Gold. "
			  "Orrin starts Advanced Artillery and seven other Expert skills. Visit the first Orrin-only "
			  "Seer Hut east at21,10 for1000XP: choose the sole Artillery upgrade to Expert. There must "
			  "be NO mastery choice at that level. Save normally and inspect development. The second "
			  "Seer Hut at21,14 awards another1000XP: after the ordinary level dialog, explicitly choose "
			  "one of three masteries. Expert remains rank3. Compare first/second level and fresh reload. "
			  "Blue Valeska has Expert Artillery,999XP,100Archers and a Ballista near8neutralPikemen "
			  "at26,26. End turns normally to observe actual AI progression; no scripted AI action is supplied."
			: "Mastery diagnostic, not a balanced scenario. Red human, Blue computer, Gold. Orrin "
			  "starts Expert Artillery and Basic Pathfinding with a Ballista. Visit the Orrin-only Seer "
			  "Hut east at21,10 for1000XP. Complete the normal secondary choice, then explicitly choose "
			  "Volley, Precision or Repair in the mandatory mastery dialog. No rank4, cancel or automatic "
			  "choice. Inspect saved development, save normally, quit/reload and continue. Hostile40Archers "
			  "at24,10 allow ordinary combat after the choice; native tests separately check all three effects. "
			  "Blue Valeska has Expert Artillery,999XP,100Archers and a Ballista near8neutralPikemen "
			  "at26,26. End turns normally for an AI encounter and mastery choice; routes are not scripted.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, HeroTypeID(0), PlayerColor(0)).heroExperience(0);
	if(newlyExpert)
		builder.heroSecondarySkills({{SecondarySkill::ARTILLERY, 2}, {SecondarySkill::BALLISTICS, 3},
			{SecondarySkill::FIRST_AID, 3}, {SecondarySkill::LOGISTICS, 3}, {SecondarySkill::PATHFINDING, 3},
			{SecondarySkill::SCOUTING, 3}, {SecondarySkill::NAVIGATION, 3}, {SecondarySkill::DIPLOMACY, 3}});
	else
		builder.heroSecondarySkills({{SecondarySkill::ARTILLERY, 3}, {SecondarySkill::PATHFINDING, 1}});
	builder.heroGarrison({{pikeman, 100}})
		.heroEquipped({{ArtifactPosition::MACH1, ArtifactID::BALLISTA}})
		.seerHut({21, 10, 0}, Builder::missionHero(HeroTypeID(0)), Builder::rewardExperience(1000))
		.hero({29, 26, 0}, HeroTypeID(1), PlayerColor(1)).heroExperience(999)
		.heroSecondarySkills({{SecondarySkill::ARTILLERY, 3}, {SecondarySkill::PATHFINDING, 1}})
		.heroGarrison({{archer, 100}}).heroEquipped({{ArtifactPosition::MACH1, ArtifactID::BALLISTA}})
		.monster({26, 26, 0}, pikeman, 8, 4)
		.monster({24, 10, 0}, archer, 40, 4);
	if(newlyExpert)
		builder.seerHut({21, 14, 0}, Builder::missionHero(HeroTypeID(0)), Builder::rewardExperience(1000));
	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	const auto header = parser.loadMapHeader(ResourcePath(name));
	ASSERT_NE(header, nullptr);
	ASSERT_EQ(header->version, EMapFormat::SOD);
	// No mapLoaded override, primary override, post-init rules or saved-state edits.
	startWithMap(builder);
	ASSERT_EQ(gameState()->getHeroMasteryRules(), canonical);
	const auto * human = findHeroByOwner(PlayerColor(0));
	const auto * computer = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(human, nullptr);
	ASSERT_NE(computer, nullptr);
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	for(const auto * hero : {human, computer})
	{
		ASSERT_TRUE(hero->getMasteryView());
		EXPECT_EQ(hero->getMasteryState().rules, canonical);
		EXPECT_TRUE(hero->getMasteryView()->choices.empty());
		EXPECT_FALSE(hero->getMasteryView()->pending);
		EXPECT_EQ(hero->level, 1u);
		EXPECT_TRUE(hero->hasArt(ArtifactID::BALLISTA, true));
		EXPECT_FALSE(hero->hasSpellbook());
	}
	EXPECT_EQ(human->exp, 0);
	EXPECT_EQ(computer->exp, 999);
	EXPECT_EQ(human->getSecSkillLevel(SecondarySkill::ARTILLERY), newlyExpert ? 2 : 3);
	EXPECT_EQ(computer->getSecSkillLevel(SecondarySkill::ARTILLERY), 3);
	EXPECT_EQ(human->getMasteryView()->eligibleNextLevel.empty(), newlyExpert);
	ASSERT_NE(findObjectAt({21, 10, 0}), nullptr);
	if(newlyExpert)
		ASSERT_NE(findObjectAt({21, 14, 0}), nullptr);
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

INSTANTIATE_TEST_SUITE_P(ExpertBoundary, NewHorizonsMasteryFixtureExportTest, ::testing::Bool());
