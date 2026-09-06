/*
 * HeroCommandFixtureExportTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include <cstdlib>
#include <zlib.h>

class HeroCommandFixtureExportTest : public HeroCommandFixture, public ::testing::WithParamInterface<bool>
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings); // Gold, not a random stat-changing artifact.
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_P(HeroCommandFixtureExportTest, ExportValidatedOrdinaryHeroBattle)
{
	const auto * enabled = std::getenv("NH_EXPORT_COMMAND_FIXTURES");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned export requires NH_EXPORT_COMMAND_FIXTURES=1";

	const bool spellAI = GetParam();
	const std::string name = spellAI ? "NHCommandsSpellAI" : "NHCommandsBooklessAI";
	const auto melee = creatureByName("dendroidGuard");
	const auto ranged = creatureByName("grandElf");
	const std::vector<std::pair<CreatureID, uint16_t>> army = {{melee, 600}, {ranged, 80}};
	const std::vector<SpellID> humanSpells = {SpellID::HASTE, SpellID::BLOODLUST, SpellID::MAGIC_ARROW};
	const std::vector<SpellID> computerSpells = spellAI
		? std::vector<SpellID>{SpellID::MAGIC_ARROW, SpellID::IMPLOSION} : std::vector<SpellID>{};
	std::vector<std::pair<ArtifactPosition, ArtifactID>> computerEquipment = {
		{ArtifactPosition::MACH1, ArtifactID::BALLISTA}
	};
	if(spellAI)
		computerEquipment.emplace_back(ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK);

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("New Horizons command diagnostic. Play Red; leave Blue computer-controlled. "
			"Select Gold starting bonus for both players. Ordinary hero combat: save before approach. "
			"No neutral garrison and no scripted state injection. Durable armies permit round-boundary checks; "
			"survival still depends on normal actions. This is not a balanced scenario.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, HeroTypeID(0), PlayerColor(0))
		.heroExperience(0).heroPrimary(2, 2, 3, 10)
		.heroSecondarySkills({{SecondarySkill::PATHFINDING, 1}})
		.heroGarrison(army).heroSpells(humanSpells)
		.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK},
			{ArtifactPosition::MACH1, ArtifactID::BALLISTA}})
		.hero({20, 10, 0}, HeroTypeID(2), PlayerColor(1))
		.heroExperience(0).heroPrimary(2, 2, spellAI ? 99 : 3, 10)
		.heroSecondarySkills({{SecondarySkill::PATHFINDING, 1}})
		.heroGarrison(army).heroSpells(computerSpells).heroEquipped(computerEquipment);

	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	const auto header = parser.loadMapHeader(ResourcePath(name));
	ASSERT_NE(header, nullptr);
	ASSERT_EQ(header->version, EMapFormat::SOD);
	ASSERT_EQ(header->width, 36);
	ASSERT_EQ(header->height, 36);
	ASSERT_EQ(header->levels(), 1);
	ASSERT_TRUE(header->players[0].canHumanPlay);
	ASSERT_TRUE(header->players[1].canComputerPlay);

	// Standard initialization of authored intent, not mutation of a running GUI.
	// startWithMap only changes its COPY's diagnostic name when dumping its test map.
	startWithMap(builder);
	ASSERT_EQ(gameState()->getHeroCommandRules()["rulesetVersion"].Integer(), 1);
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	const auto * human = findHeroByOwner(PlayerColor(0));
	const auto * computer = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(human, nullptr);
	ASSERT_NE(computer, nullptr);
	ASSERT_EQ(human->getHeroTypeID(), HeroTypeID(0));
	ASSERT_EQ(computer->getHeroTypeID(), HeroTypeID(2));
	ASSERT_EQ(human->pos, int3(17, 10, 0));
	ASSERT_EQ(computer->pos, int3(20, 10, 0));
	ASSERT_TRUE(human->hasSpellbook());
	ASSERT_EQ(computer->hasSpellbook(), spellAI);
	ASSERT_EQ(human->getSpellsInSpellbook(), std::set<SpellID>(humanSpells.begin(), humanSpells.end()));
	ASSERT_EQ(computer->getSpellsInSpellbook(), std::set<SpellID>(computerSpells.begin(), computerSpells.end()));
	for(const auto * hero : {human, computer})
	{
		ASSERT_EQ(hero->getPrimSkillLevel(PrimarySkill::ATTACK), 2);
		ASSERT_EQ(hero->getPrimSkillLevel(PrimarySkill::DEFENSE), 2);
		ASSERT_EQ(hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 10);
		ASSERT_EQ(hero->mana, 100);
		ASSERT_EQ(hero->stacksCount(), 2);
		ASSERT_EQ(hero->getStackCount(SlotID(0)), 600);
		ASSERT_EQ(hero->getStackCount(SlotID(1)), 80);
		ASSERT_EQ(hero->getStack(SlotID(0)).getCreatureID(), melee);
		ASSERT_EQ(hero->getStack(SlotID(1)).getCreatureID(), ranged);
	}
	ASSERT_EQ(human->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 3);
	ASSERT_EQ(computer->getPrimSkillLevel(PrimarySkill::SPELL_POWER), spellAI ? 99 : 3);

	// Publish the named GUI fixture only after parser and real-init assertions pass.
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
	std::cout << "Command fixture " << name << ": " << path.string()
		<< "; human visitable=" << human->visitablePos().toString()
		<< "; AI visitable=" << computer->visitablePos().toString()
		<< "; both armies=600 dendroidGuard + 80 grandElf; both mana=100" << std::endl;
}

INSTANTIATE_TEST_SUITE_P(BooklessAndSpell, HeroCommandFixtureExportTest, ::testing::Bool());
