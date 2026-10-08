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
#include <array>
#include <set>
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
	// Authored Knowledge is unchanged; only this hero's saved mana scale differs.
	// Preserve the original 200-mana oracle for legacy primary rules.
	ASSERT_EQ(human->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 20);
	const int expectedHumanMana = human->usesPrimaryGrowth() ? 20 : 200;
	ASSERT_EQ(human->getManaAvailable(), expectedHumanMana);
	ASSERT_EQ(human->manaLimit(), expectedHumanMana);
	ASSERT_EQ(computer->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 10);
	const int expectedComputerMana = computer->usesPrimaryGrowth() ? 10 : 100;
	ASSERT_EQ(computer->getManaAvailable(), expectedComputerMana);
	ASSERT_EQ(computer->manaLimit(), expectedComputerMana);
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

TEST_F(NewHorizonsMagicFixtureExportTest, ExportOrdinarySixSchoolCastingScenario)
{
	const auto * enabled = std::getenv("NH_EXPORT_MAGIC_FIXTURES");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned opt-in export requires NH_EXPORT_MAGIC_FIXTURES=1";
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires actual curated native preset";

	const std::string name = "NHMagicCastingSixSchools";
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsMagic"));
	std::vector<SpellID> spells;
	for(const auto id : LIBRARY->spellh->getDefaultAllowed())
	{
		const auto * spell = id.toSpell();
		if(spell->getModScope() != ModScope::scopeBuiltin() || !spell->isCombat())
			continue;
		const auto key = spell->getJsonKey();
		const auto & roster = canonical["spells"];
		if(!roster.isStruct() || !roster.Struct().contains(key))
			continue;
		const auto & entry = roster[key];
		if((entry["active"].isBool() && !entry["active"].Bool())
			|| (entry["heroAccess"].isBool() && !entry["heroAccess"].Bool())
			|| (entry["ordinaryAcquisition"].isBool() && !entry["ordinaryAcquisition"].Bool()))
			continue;
		spells.push_back(id);
	}
	ASSERT_FALSE(spells.empty());
	// SoD primary stats and spell masks are byte-bounded: author Knowledge200
	// and current legal core spells, never truncate500 or encode add-on IDs.
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("Six-school casting-color diagnostic. Human Red, Blue computer, Gold bonuses. "
			"Approach Blue's adjacent hero and use ordinary manual combat. Red knows every currently "
			"allowed core combat spell, has Knowledge200, Expert Wisdom and no authored school ranks. "
			"Cast Bless (Light) on friendly troops; Quicksand (Nature) on an empty battlefield hex; "
			"Magic Arrow (Sorcery), Lightning Bolt (Havoc), Curse (Shadow), Misfortune (Chaos) on "
			"durable enemy Dendroid Guards. Defend troops between hero actions. No injected saved rules, "
			"cheats, altered creature stats or add-on spell-mask aliases. Not a balanced scenario.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, HeroTypeID(0), PlayerColor(0))
		.heroExperience(0).heroPrimary(2, 2, 6, 200)
		.heroSecondarySkills({{SecondarySkill::WISDOM, 3}})
		.heroGarrison({{creatureByName("dendroidGuard"), 5000}, {creatureByName("pikeman"), 10}})
		.heroSpells(spells).heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
		.hero({19, 10, 0}, HeroTypeID(2), PlayerColor(1))
		.heroExperience(0).heroPrimary(2, 2, 3, 10)
		.heroSecondarySkills({{SecondarySkill::PATHFINDING, 1}})
		.heroGarrison({{creatureByName("dendroidGuard"), 1500}}).heroSpells({});
	const auto expected = builder.build();
	startWithMap(builder); // Ordinary module initialization; no mapLoaded override.
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	const auto * human = findHeroByOwner(PlayerColor(0));
	const auto * computer = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(human, nullptr);
	ASSERT_NE(computer, nullptr);
	ASSERT_TRUE(human->hasSpellbook());
	ASSERT_FALSE(computer->hasSpellbook());
	ASSERT_EQ(human->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 200);
	ASSERT_EQ(human->getSpellsInSpellbook(), std::set<SpellID>(spells.begin(), spells.end()));
	ASSERT_EQ(human->getStackCount(SlotID(0)), 5000);
	ASSERT_EQ(human->getStackCount(SlotID(1)), 10);
	ASSERT_EQ(computer->getStackCount(SlotID(0)), 1500);
	const std::set<std::string> expectedSchools{
		"new-horizons:light", "new-horizons:nature", "new-horizons:sorcery",
		"new-horizons:havoc", "new-horizons:shadow", "new-horizons:chaos"};
	ASSERT_EQ(gameState()->getActiveSpellSchools().size(), 6u);
	std::set<std::string> actualSchools;
	for(const auto school : gameState()->getActiveSpellSchools())
	{
		const auto bare = SpellSchool::encode(school.getNum());
		EXPECT_EQ(bare.find(':'), std::string::npos);
		EXPECT_EQ(school.serializationKey(), "new-horizons:" + bare);
		actualSchools.insert(school.serializationKey());
	}
	ASSERT_EQ(actualSchools, expectedSchools);
	const std::array<std::pair<const char *, const char *>, 6> representatives{{
		{"core:bless", "new-horizons:light"}, {"core:quicksand", "new-horizons:nature"},
		{"core:magicArrow", "new-horizons:sorcery"}, {"core:lightningBolt", "new-horizons:havoc"},
		{"core:curse", "new-horizons:shadow"}, {"core:misfortune", "new-horizons:chaos"}}};
	int64_t totalMana = 0;
	for(const auto & [spellKey, schoolKey] : representatives)
	{
		const auto spellId = SpellID(SpellID::decode(spellKey));
		ASSERT_NE(spellId, SpellID::NONE);
		ASSERT_TRUE(vstd::contains(spells, spellId)) << spellKey;
		ASSERT_TRUE(human->canCastThisSpell(spellId.toSpell())) << spellKey;
		const auto schools = human->getSpellSchools(spellId.toSpell());
		ASSERT_EQ(schools.size(), 1u) << spellKey;
		EXPECT_EQ(schools.front().serializationKey(), schoolKey);
		const auto cost = human->getSpellCost(spellId.toSpell());
		ASSERT_GT(cost, 0);
		totalMana += cost;
		std::cout << name << " representative=" << spellKey << " school=" << schoolKey << " mana=" << cost << '\n';
	}
	EXPECT_LE(totalMana, 100);
	ASSERT_GE(human->getManaAvailable(), totalMana);
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
		<< "; legal core combat spells=" << spells.size() << "; six-cast mana=" << totalMana << std::endl;
}
