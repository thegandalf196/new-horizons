/*
 * NewHorizonsMagicFixtureExportTest.cpp, part of VCMI engine
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
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include <cstdlib>
#include <zlib.h>

class NewHorizonsMagicFixtureExportTest : public BattleTestFixture
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_F(NewHorizonsMagicFixtureExportTest, ExportOrdinaryFullBookWithStartingRankConversion)
{
	const auto * enabled = std::getenv("NH_EXPORT_MAGIC_FIXTURES");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned opt-in export requires NH_EXPORT_MAGIC_FIXTURES=1";
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Export verification requires the actual curated native preset";

	const std::string name = "NHMagicFullBookRanks";
	std::vector<SpellID> originalSpells;
	int combatSpells = 0;
	for(const auto spell : LIBRARY->spellh->getDefaultAllowed())
	{
		if(spell.toSpell()->getModScope() != ModScope::scopeBuiltin())
			continue;
		originalSpells.push_back(spell);
		combatSpells += spell.toSpell()->isCombat();
	}
	ASSERT_GT(combatSpells, 24) << "Must exercise paging even in the large spellbook";
	const std::vector<std::pair<SecondarySkill, uint8_t>> authoredRanks = {
		{SecondarySkill::FIRE_MAGIC, 3}, {SecondarySkill::AIR_MAGIC, 2},
		{SecondarySkill::WATER_MAGIC, 1}, {SecondarySkill::EARTH_MAGIC, 1}
	};
	const std::vector<std::pair<CreatureID, uint16_t>> army = {
		{creatureByName("dendroidGuard"), 1500}, {creatureByName("grandElf"), 160}
	};
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("School UI diagnostic, not a balanced scenario. Play Red, Blue computer, Gold bonuses both. "
			"Red owns the full original common spellbook and four map-authored elemental ranks. "
			"Ordinary curated initialization converts them: Havoc Expert, Sorcery Advanced, Light/Nature Basic; "
			"Shadow/Chaos initially untrained. Both armies 1500 Dendroid Guards + 160 Grand Elves. "
			"Red mana 200, Blue bookless. Save before approach. Use normal spell/command/creature actions; "
			"survival depends on play. Existing command fixtures are separate and unchanged.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, HeroTypeID(0), PlayerColor(0))
		.heroExperience(0).heroPrimary(2, 2, 6, 20).heroSecondarySkills(authoredRanks)
		.heroGarrison(army).heroSpells(originalSpells)
		.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}, {ArtifactPosition::MACH1, ArtifactID::BALLISTA}})
		.hero({20, 10, 0}, HeroTypeID(2), PlayerColor(1))
		.heroExperience(0).heroPrimary(2, 2, 3, 10)
		.heroSecondarySkills({{SecondarySkill::PATHFINDING, 1}})
		.heroGarrison(army).heroSpells({})
		.heroEquipped({{ArtifactPosition::MACH1, ArtifactID::BALLISTA}});

	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	const auto header = parser.loadMapHeader(ResourcePath(name));
	ASSERT_NE(header, nullptr);
	ASSERT_EQ(header->version, EMapFormat::SOD);
	ASSERT_EQ(header->width, 36);
	ASSERT_EQ(header->height, 36);
	// No mapLoaded override and no saved-rule injection: actual module settings.
	startWithMap(builder);
	ASSERT_EQ(gameState()->getActiveSpellSchools().size(), 6u);
	ASSERT_EQ(gameState()->getMagicRules()["rulesetVersion"].Integer(), 1);
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	const auto * human = findHeroByOwner(PlayerColor(0));
	const auto * computer = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(human, nullptr);
	ASSERT_NE(computer, nullptr);
	ASSERT_TRUE(human->hasSpellbook());
	ASSERT_FALSE(computer->hasSpellbook());
	ASSERT_EQ(human->mana, 200);
	ASSERT_EQ(human->getSpellsInSpellbook(), std::set<SpellID>(originalSpells.begin(), originalSpells.end()));
	for(const auto & [oldSkill, rank] : authoredRanks)
	{
		const auto effective = newHorizonsMagic::replacementSkill(gameState()->getMagicRules(), oldSkill);
		ASSERT_NE(effective, oldSkill);
		ASSERT_EQ(human->getSecSkillLevel(oldSkill), 0);
		ASSERT_EQ(human->getSecSkillLevel(effective), rank);
	}
	for(const auto school : gameState()->getActiveSpellSchools())
	{
		int count = 0;
		for(const auto spell : originalSpells)
			count += vstd::contains(human->getSpellSchools(spell.toSpell()), school);
		ASSERT_GT(count, 0) << school.serializationKey();
		std::cout << name << " school=" << school.serializationKey() << " known=" << count << '\n';
	}
	for(const auto * hero : {human, computer})
	{
		ASSERT_EQ(hero->getStackCount(SlotID(0)), 1500);
		ASSERT_EQ(hero->getStackCount(SlotID(1)), 160);
	}

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
	std::cout << "Magic fixture " << name << ": " << path.string()
		<< "; original spells=" << originalSpells.size() << "; combat spells=" << combatSpells << std::endl;
}
