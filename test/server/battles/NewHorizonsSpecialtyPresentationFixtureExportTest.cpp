/*
 * NewHorizonsSpecialtyPresentationFixtureExportTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "mock/TinyMapGameTest.h"
#include "../../../client/windows/HeroSpecialtyPresentation.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/filesystem/Filesystem.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <zlib.h>

namespace
{
struct SpecialtyCase
{
	const char * identity;
	const char * artwork;
	int3 position;
	const char * creatureTarget = nullptr;
};

const std::array<SpecialtyCase, 12> heroes = {{
	{"core:serena", "newHorizons/heroSpecialties/SerenaArcanePreparation", {17, 10, 0}},
	{"core:gem", "newHorizons/heroSpecialties/GemRestorativeSorceress", {18, 10, 0}},
	{"core:daremyth", "newHorizons/heroSpecialties/DaremythFortunesChild", {17, 11, 0}},
	{"core:orrin", nullptr, {18, 11, 0}},
	{"core:monere", "newHorizons/heroSpecialties/MonereWispLine", {19, 10, 0}, "new-horizons:wisp"},
	{"core:gerwulf", "newHorizons/heroSpecialties/GerwulfBallistaLine", {19, 11, 0}, "core:ballista"},
	{"core:elleshar", "newHorizons/heroSpecialties/EllesharHighLore", {20, 10, 0}},
	{"core:malcom", "newHorizons/heroSpecialties/MalcomNaturalPhilosopher", {20, 11, 0}},
	{"core:andra", "newHorizons/heroSpecialties/AndraBottomlessMind", {21, 10, 0}},
	{"core:thorgrim", "newHorizons/heroSpecialties/ThorgrimMasterSpellward", {21, 11, 0}},
	{"core:pasis", "newHorizons/heroSpecialties/PasisWispLine", {22, 10, 0}, "new-horizons:wisp"},
	{"core:christian", "newHorizons/heroSpecialties/ChristianBallistaLine", {22, 11, 0}, "core:ballista"}
}};
}

// Ordinary starts only: no BattleTestFixture combat setup or rule overrides.
class NewHorizonsSpecialtyPresentationFixtureExportTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }

	void configurePlayer(PlayerSettings & player) const override
	{
		player.bonus = PlayerStartingBonus::GOLD;
		if(player.color == PlayerColor(1))
			player.connectedPlayerIDs.clear();
	}

	void expectConvertedCreatureLine(const CGHeroInstance & hero, CreatureID target)
	{
		ASSERT_EQ(hero.getCreatureLineSpecialtyTarget(), target);
		ASSERT_EQ(hero.getSpecialtyPresentation().creature, target);
		const auto prefix = "new-horizons:creature-line-specialty:"
			+ std::to_string(hero.getHeroTypeID().getNum()) + ":" + std::to_string(target.getNum()) + ":";
		const auto & bonuses = hero.getExportedBonusList();
		for(const char * stat : {"speed", "initiative", "attack", "defense"})
		{
			const auto marker = prefix + stat;
			ASSERT_EQ(std::count_if(bonuses.begin(), bonuses.end(), [&marker](const auto & bonus)
			{
				return bonus && bonus->stacking == marker;
			}), 1);
		}
	}

	void expectOrdinaryStart(const SpecialtyCase & row, const JsonNode & development,
		const JsonNode & perks, const JsonNode & magic)
	{
		SCOPED_TRACE(row.identity);
		const auto * hero = findHeroAt(row.position);
		ASSERT_NE(hero, nullptr);
		ASSERT_EQ(hero->getHeroTypeID(), HeroTypeID(HeroTypeID::decode(row.identity)));
		ASSERT_EQ(hero->getOwner(), PlayerColor(0));
		ASSERT_EQ(hero->level, 1);
		const auto & profile = development["startingSkills"]["startingDevelopmentProfiles"][row.identity];
		ASSERT_FALSE(profile.isNull());
		ASSERT_EQ(hero->secSkills.size(), profile["skills"].Vector().size());
		for(const auto & skill : profile["skills"].Vector())
			ASSERT_EQ(hero->getPerkSkillRank(skill["skill"].String()), skill["rank"].Integer());
		std::vector<newHorizonsHeroes::PerkSelection> selected;
		for(const auto & perk : profile["startingPerks"].Vector())
			selected.push_back({perk["skill"].String(), perk["perk"].String()});
		ASSERT_EQ(hero->getPerkState().selected, selected);
		ASSERT_EQ(hero->getPerkState().rules, perks);
		ASSERT_EQ(hero->getMagicRules(), magic);
		auto expectedGrowth = newHorizonsHeroes::resolveHeroRules(development, hero->getHeroClassID());
		if(const auto target = newHorizonsHeroes::defaultCreatureLineTarget(expectedGrowth, hero->getHeroTypeID()))
			expectedGrowth["creatureLineSpecialtyTarget"].String() = CreatureID::encode(target->getNum());
		ASSERT_EQ(hero->getPrimaryGrowthRules(), expectedGrowth);
		if(row.creatureTarget)
		{
			ASSERT_NO_FATAL_FAILURE(expectConvertedCreatureLine(*hero, CreatureID(CreatureID::decode(row.creatureTarget))));
		}
		const auto metadata = hero->getSpecialtyPresentation();
		if(row.artwork)
		{
			ASSERT_TRUE(metadata.artwork);
			ASSERT_EQ(*metadata.artwork, row.artwork);
		}
		else
		{
			ASSERT_FALSE(metadata.artwork);
			// Specialty identity retains the core alias; starting Skills use the NH registry.
			const auto & alias = hero->getHeroType()->secondarySkillSpecialtyAlias;
			ASSERT_TRUE(alias);
			ASSERT_EQ(alias->skill, SecondarySkill(SecondarySkill::ARCHERY));
			ASSERT_EQ(metadata.skill, alias->skill);
			ASSERT_FALSE(metadata.spell);
			ASSERT_FALSE(metadata.creature);
			const auto view = heroSpecialtyPresentation(*hero->getHeroType(), metadata);
			ASSERT_EQ(view.animation().getOriginalName(), "SECSKILL");
			ASSERT_EQ(view.animation(true).getOriginalName(), "SECSK32");
			ASSERT_EQ(view.frame(), alias->skill.toEntity(LIBRARY)->getIconIndex());
		}
	}

	void expectExportBytes(const std::string & name, const std::vector<uint8_t> & expected)
	{
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
};

TEST_F(NewHorizonsSpecialtyPresentationFixtureExportTest, ExportOrdinarySpecialtyPresentationMap)
{
	const auto * enabled = std::getenv("NH_EXPORT_SPECIALTY_PRESENTATION_FIXTURE");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Private opt-in export requires NH_EXPORT_SPECIALTY_PRESENTATION_FIXTURE=1";
	ASSERT_TRUE(newHorizonsHeroes::usesRules(
		LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS)))
		<< "Requires the activated New Horizons preset; no fixture rule overrides";
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const std::string name = "NHSpecialtyPresentation";
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("Specialty presentation diagnostic, not a balanced scenario or gameplay certification. "
			"Red owns adjacent Serena, Gem, Daremyth, ordinary control Orrin, Monere, Gerwulf, Elleshar, Malcom, Andra, Thorgrim, Pasis and Christian with authored starts; "
			"no map Skill, perk, experience, primary, army or spellbook overrides. "
			"Open each normal Hero Window and meet adjacent heroes through normal exchange. "
			"Inspect specialty icons and tooltips, then save/reload and inspect again. "
			"Red owns Tower and Rampart; distant Blue Castle keeps the world active.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::TOWER, PlayerColor(0)).townGarrison({})
		.town({8, 18, 0}, FactionID::RAMPART, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({});
	// Omit all hero customisation setters, retaining each ordinary authored start.
	for(const auto & row : heroes)
		builder.hero(row.position, HeroTypeID(HeroTypeID::decode(row.identity)), PlayerColor(0));
	const auto expected = builder.build();
	startWithMap(builder);
	ASSERT_EQ(gameState()->getHeroDevelopmentRules(), development);
	ASSERT_EQ(gameState()->getHeroPerkRules(), perks);
	ASSERT_EQ(gameState()->getMagicRules(), magic);
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	for(const auto & row : heroes)
		ASSERT_NO_FATAL_FAILURE(expectOrdinaryStart(row, development, perks, magic));
	ASSERT_EQ(builder.buildAndDump(name), expected);
	ASSERT_NO_FATAL_FAILURE(expectExportBytes(name, expected));
}
