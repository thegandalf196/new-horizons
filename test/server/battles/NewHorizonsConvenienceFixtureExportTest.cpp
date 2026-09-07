/*
 * NewHorizonsConvenienceFixtureExportTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include <cstdlib>
#include <zlib.h>

class NewHorizonsConvenienceFixtureExportTest : public BattleTestFixture
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_F(NewHorizonsConvenienceFixtureExportTest, ExportOrdinaryArmyForSaveButtonsAndTruthfulAbilityCards)
{
	const auto * enabled = std::getenv("NH_EXPORT_CONVENIENCE_FIXTURE");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned opt-in export requires NH_EXPORT_CONVENIENCE_FIXTURE=1";
	const std::string name = "NHConvenienceArmy";
	const auto pikeman = creatureByName("core:pikeman");
	const auto vampire = creatureByName("core:vampire");
	const auto vampireLord = creatureByName("core:vampireLord");
	const auto naga = creatureByName("core:naga");
	using Builder = TinyH3M::TinyH3MBuilder;
	Builder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("Presentation diagnostic, not a balanced scenario. Red human, Blue computer. "
			"Orrin starts with100Pikemen,20Vampires,20VampireLords,20Nagas, Expert Artillery and a Ballista. "
			"Inspect ordinary army cards: both Vampire types are undead and block retaliation, but ONLY "
			"Vampire Lords drain life. Nagas block retaliation without being undead. Pikemen are a normal "
			"control. The equipped Ballista appears in ordinary battle and has the siege-weapon property. "
			"The mixed army retains normal morale rules. Optional200neutralPikemen at24,10 provide combat; "
			"do not infer ability execution from icons alone. Test visible quick-save/load buttons using "
			"ordinary movement and fresh loading; F8/F9 remain existing controls. No authored XP reward, mastery "
			"selection, altered primary stats, scripted AI action or special save state is supplied.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, HeroTypeID(0), PlayerColor(0)).heroExperience(0)
		.heroSecondarySkills({{SecondarySkill::ARTILLERY, 3}})
		.heroGarrison({{pikeman, 100}, {vampire, 20}, {vampireLord, 20}, {naga, 20}})
		.heroEquipped({{ArtifactPosition::MACH1, ArtifactID::BALLISTA}})
		.monster({24, 10, 0}, pikeman, 200, 4);
	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	const auto header = parser.loadMapHeader(ResourcePath(name));
	ASSERT_NE(header, nullptr);
	ASSERT_EQ(header->version, EMapFormat::SOD);
	// Ordinary H3M initialization only: no mapLoaded/rule/primary/bonus overrides.
	startWithMap(builder);
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	EXPECT_EQ(hero->level, 1u);
	EXPECT_EQ(hero->exp, 0);
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::ARTILLERY), 3);
	EXPECT_TRUE(hero->hasArt(ArtifactID::BALLISTA, true));
	EXPECT_FALSE(hero->hasSpellbook());
	const std::array<CreatureID, 4> types = {pikeman, vampire, vampireLord, naga};
	std::array<const CStackInstance *, 4> army{};
	for(size_t slot = 0; slot < types.size(); ++slot)
	{
		army[slot] = hero->getStackPtr(SlotID(static_cast<int>(slot)));
		ASSERT_NE(army[slot], nullptr);
		EXPECT_EQ(army[slot]->getCreatureID(), types[slot]);
		EXPECT_EQ(army[slot]->getCount(), slot == 0 ? 100 : 20);
		EXPECT_EQ(army[slot]->hasBonusOfType(BonusType::UNDEAD), slot == 1 || slot == 2);
		EXPECT_EQ(army[slot]->hasBonusOfType(BonusType::BLOCKS_RETALIATION), slot != 0);
	}
	// Life drain is the actual script trigger, not the deprecated LIFE_DRAIN enum.
	const std::string scriptName = "core:lifeDrain";
	const auto script = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script", scriptName);
	ASSERT_TRUE(script.has_value());
	for(size_t slot = 0; slot < army.size(); ++slot)
		EXPECT_EQ(army[slot]->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER, BonusSubtypeID(ScriptID(*script))), slot == 2);
	EXPECT_TRUE(CreatureID(CreatureID::BALLISTA).toCreature()->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_NE(findObjectAt({24, 10, 0}), nullptr);
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
