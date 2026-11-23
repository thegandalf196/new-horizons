/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/entities/hero/NewHorizonsStartingPerks.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../mock/TinyMapGameTest.h"
#include <map>
#include <set>

namespace
{
using namespace newHorizonsHeroes;

JsonNode development() { return JsonNode(JsonPath::builtin("config/newHorizonsHeroes")); }

StartingHeroContext context(JsonNode rules = development())
{
	StartingHeroContext result;
	result.development = std::move(rules);
	result.capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
	PerkState perks;
	perks.replaceRules(JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	result.perks = perks.getRules();
	result.magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	return result;
}

const CHero & hero(const char * key)
{
	return *HeroTypeID(HeroTypeID::decode(key)).toHeroType();
}

SecondarySkill skill(const char * key)
{
	return SecondarySkill(SecondarySkill::decode(key));
}

SpellID spell(const char * key)
{
	return SpellID(SpellID::decode(key));
}

class CastleStartingBookTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
};

/// A 1164/1165 capture: Cuthbert's two-Skill row, his perk and the 22-row starting book table.
JsonNode historicalStarts()
{
	auto rules = development();
	auto & starts = rules["startingSkills"];
	starts["startingBookReplacements"].Struct().erase("core:cuthbert");
	starts["startingBookReplacements"].Struct().erase("core:loynis");
	auto & skills = starts["startingDevelopmentProfiles"]["core:cuthbert"]["skills"].Vector();
	skills.pop_back();
	skills.back()["skill"].String() = "new-horizons:spellcraft";
	JsonNode perk;
	perk["skill"].String() = "new-horizons:spellcraft";
	perk["perk"].String() = "new-horizons:spellcraft.spellPenetration";
	starts["startingPerkProfiles"]["core:cuthbert"].Vector() = {perk};
	return rules;
}
}

using CastleStartingProfiles = vcmiTestData::OriginalDataTest;

TEST_F(CastleStartingProfiles, CuthbertStartsWithThreeBasicSkillsNoPerkAndHolyArmor)
{
	if(!SpellID::decode("new-horizons:holyArmor"))
		GTEST_SKIP() << "Requires New Horizons content";
	const auto view = projectStartingHero(hero("core:cuthbert"), context());
	const std::set<std::pair<SecondarySkill, ui8>> skills(view.skills.begin(), view.skills.end());
	EXPECT_EQ(skills, (std::set<std::pair<SecondarySkill, ui8>>{
		{skill("new-horizons:divineMandate"), MasteryLevel::BASIC},
		{skill("new-horizons:lightMagic"), MasteryLevel::BASIC},
		{skill("new-horizons:shadowMagic"), MasteryLevel::BASIC}}));
	EXPECT_TRUE(view.perks.empty());
	EXPECT_TRUE(view.spellBook);
	EXPECT_TRUE(view.spells.contains(spell("new-horizons:holyArmor")));
	EXPECT_FALSE(view.spells.contains(SpellID(SpellID::WEAKNESS)));
}

TEST_F(CastleStartingProfiles, LoynisStartsWithSanctuaryInsteadOfTheChosen)
{
	if(!SpellID::decode("new-horizons:sanctuary"))
		GTEST_SKIP() << "Requires New Horizons content";
	const auto view = projectStartingHero(hero("core:loynis"), context());
	EXPECT_TRUE(view.spells.contains(spell("new-horizons:sanctuary")));
	EXPECT_FALSE(view.spells.contains(spell("new-horizons:theChosen")));
	EXPECT_EQ(view.perks.size(), 1u);
}

TEST_F(CastleStartingProfiles, HistoricalCapturesKeepTheirStarts)
{
	if(!SpellID::decode("new-horizons:theChosen"))
		GTEST_SKIP() << "Requires New Horizons content";
	const auto historical = historicalStarts();
	EXPECT_NO_THROW(validateStartingPerkProfiles(historical["startingSkills"]));
	const auto cuthbert = projectStartingHero(hero("core:cuthbert"), context(historical));
	EXPECT_EQ(cuthbert.skills.size(), 2u);
	EXPECT_EQ(cuthbert.perks.size(), 1u);
	EXPECT_FALSE(cuthbert.spells.contains(spell("new-horizons:holyArmor")));
	const auto loynis = projectStartingHero(hero("core:loynis"), context(historical));
	EXPECT_TRUE(loynis.spells.contains(spell("new-horizons:theChosen")));
}

TEST_F(CastleStartingProfiles, OnlyAnExplicitlyEmptyPerkRowAllowsThreeSkills)
{
	auto rules = development();
	auto & starts = rules["startingSkills"];
	EXPECT_TRUE(usesThreeSkillStart(starts, "core:cuthbert"));
	EXPECT_FALSE(usesThreeSkillStart(starts, "core:adela"));
	auto withPerk = rules;
	JsonNode perk;
	perk["skill"].String() = "new-horizons:lightMagic";
	perk["perk"].String() = "new-horizons:lightMagic.healer";
	withPerk["startingSkills"]["startingPerkProfiles"]["core:cuthbert"].Vector() = {perk};
	EXPECT_THROW(validateStartingPerkProfiles(withPerk["startingSkills"]), std::runtime_error);
	auto emptyTwoSkills = rules;
	emptyTwoSkills["startingSkills"]["startingPerkProfiles"]["core:adela"].Vector().clear();
	EXPECT_THROW(validateStartingPerkProfiles(emptyTwoSkills["startingSkills"]), std::runtime_error);
	auto partialBooks = rules;
	partialBooks["startingSkills"]["startingBookReplacements"].Struct().erase("core:loynis");
	EXPECT_THROW(validateHeroRules(partialBooks, false), std::runtime_error);
}

TEST_F(CastleStartingProfiles, NoTwoCastleHeroesShareBothSkillsAndThePerk)
{
	const auto rules = development();
	const auto & starts = rules["startingSkills"];
	std::map<std::pair<std::set<std::string>, std::string>, std::string> seen;
	for(const auto & [identity, profile] : starts["startingDevelopmentProfiles"].Struct())
	{
		const auto * prototype = HeroTypeID(HeroTypeID::decode(identity)).toHeroType();
		if(!prototype || prototype->special || prototype->heroClass->faction != FactionID::CASTLE)
			continue;
		std::set<std::string> skills;
		for(const auto & row : profile["skills"].Vector())
			skills.insert(row["skill"].String());
		const auto & perks = starts["startingPerkProfiles"][identity].Vector();
		const auto key = std::pair(skills, perks.empty() ? std::string() : perks.front()["perk"].String());
		const auto [existing, inserted] = seen.emplace(key, identity);
		EXPECT_TRUE(inserted) << identity << " repeats " << existing->second;
	}
	EXPECT_EQ(seen.size(), 16u);
}

TEST_F(CastleStartingBookTest, CuthbertsHolyArmorStartAlsoTeachesOnePariahShadowSpell)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:cuthbert")), PlayerColor(0)).heroExperience(0);
	startWithMap(std::move(builder));
	const auto * cuthbert = findHeroAt({5, 5, 0});
	ASSERT_NE(cuthbert, nullptr);
	const auto & magic = cuthbert->getMagicRules();
	const SpellSchool shadow(SpellSchool::decode("new-horizons:shadow"));
	const auto book = cuthbert->getSpellsInSpellbook();
	EXPECT_TRUE(book.contains(spell("new-horizons:holyArmor")));
	EXPECT_FALSE(book.contains(SpellID(SpellID::WEAKNESS)));
	// Pariah mirrors the Light 2 start with one random unknown Shadow 2 spell.
	ASSERT_EQ(book.size(), 2u);
	const auto mirror = std::ranges::find_if(book, [](SpellID id) { return id != spell("new-horizons:holyArmor"); });
	ASSERT_NE(mirror, book.end());
	EXPECT_EQ(newHorizonsMagic::spellLevel(magic, *mirror), 2);
	EXPECT_TRUE(vstd::contains(newHorizonsMagic::spellSchools(magic, *mirror), shadow));
	EXPECT_TRUE(cuthbert->getPerkState().getSelected().empty());
}
