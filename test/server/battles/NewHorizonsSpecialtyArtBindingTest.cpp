/*
 * NewHorizonsSpecialtyArtBindingTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../client/windows/HeroSpecialtyPresentation.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include <array>
#include <tuple>

namespace
{
struct BindingCase
{
	const char * hero;
	const char * policy;
	const char * artwork;
};

constexpr std::array<BindingCase, 3> bindings = {{
	{"core:serena", "arcanePreparationSpecialty", "newHorizons/heroSpecialties/SerenaArcanePreparation"},
	{"core:gem", "druidSpecialties", "newHorizons/heroSpecialties/GemRestorativeSorceress"},
	{"core:daremyth", "fortuneChildSpecialty", "newHorizons/heroSpecialties/DaremythFortunesChild"}
}};

const CHero & prototype(const char * id)
{
	return *HeroTypeID(HeroTypeID::decode(id)).toHeroType();
}
}

class NewHorizonsSpecialtyArtBindingTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
};

TEST_F(NewHorizonsSpecialtyArtBindingTest, ExplicitPoliciesBindThreeDistinctNativeResourcePairs)
{
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	for(const auto & row : bindings)
	{
		SCOPED_TRACE(row.hero);
		const auto & hero = prototype(row.hero);
		const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, development, magic, std::nullopt);
		ASSERT_TRUE(metadata.artwork);
		EXPECT_EQ(*metadata.artwork, row.artwork);
		const auto view = heroSpecialtyPresentation(hero, metadata);
		EXPECT_EQ(view.animation().getOriginalName(), std::string(row.artwork) + "/44");
		EXPECT_EQ(view.animation(true).getOriginalName(), std::string(row.artwork) + "/32");
		EXPECT_EQ(view.frame(), 0u);
		EXPECT_EQ(view.name, metadata.name);
		EXPECT_EQ(view.description, metadata.description);
		EXPECT_EQ(view.skill, metadata.skill);
		EXPECT_EQ(view.spell, metadata.spell);
	}
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, ExplicitArtOutranksBothSkillAndSpellAliases)
{
	for(const auto & row : bindings)
	{
		SCOPED_TRACE(row.hero);
		newHorizonsHeroes::SpecialtyPresentation metadata;
		metadata.name = "captured name";
		metadata.description = "captured description";
		metadata.skill = SecondarySkill(SecondarySkill::WISDOM);
		metadata.spell = SpellID::CURE;
		metadata.artwork = row.artwork;
		const auto view = heroSpecialtyPresentation(prototype(row.hero), metadata);
		EXPECT_EQ(view.animation().getOriginalName(), std::string(row.artwork) + "/44");
		EXPECT_EQ(view.animation(true).getOriginalName(), std::string(row.artwork) + "/32");
		EXPECT_EQ(view.frame(), 0u);
		EXPECT_EQ(view.name, metadata.name);
		EXPECT_EQ(view.description, metadata.description);
		EXPECT_EQ(view.skill, metadata.skill);
		EXPECT_EQ(view.spell, metadata.spell);
	}
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, AbsentCapturedPoliciesDoNotSelectInstalledArt)
{
	const JsonNode configured(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	for(const auto & row : bindings)
	{
		SCOPED_TRACE(row.hero);
		auto historical = configured;
		historical.Struct().erase(row.policy);
		const auto view = newHorizonsHeroes::resolveSpecialtyPresentation(
			prototype(row.hero), historical, magic, std::nullopt);
		EXPECT_FALSE(view.artwork);
		const auto legacy = newHorizonsHeroes::resolveSpecialtyPresentation(
			prototype(row.hero), JsonNode(), magic, std::nullopt);
		EXPECT_FALSE(legacy.artwork);
		const auto card = heroSpecialtyPresentation(prototype(row.hero), legacy);
		EXPECT_EQ(card.animation().getOriginalName(), "UN44");
		EXPECT_EQ(card.animation(true).getOriginalName(), "UN32");
		EXPECT_EQ(card.frame(), prototype(row.hero).imageIndex);
	}
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, SerenaNeedsActiveCapturedSpellPointRules)
{
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
		prototype("core:serena"), development, JsonNode(), std::nullopt);
	EXPECT_FALSE(metadata.artwork);
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, HistoricalDaremythKeepsHasteSuccessor)
{
	JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	development.Struct().erase("fortuneChildSpecialty");
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
		prototype("core:daremyth"), development, magic, std::nullopt);
	EXPECT_FALSE(metadata.artwork);
	EXPECT_EQ(metadata.spell, SpellID(SpellID::HASTE));
	const auto view = heroSpecialtyPresentation(prototype("core:daremyth"), metadata);
	EXPECT_EQ(view.animation().getOriginalName(), "SPELLSCR");
	EXPECT_EQ(view.animation(true).getOriginalName(), "SPELLSCR");
	EXPECT_EQ(view.frame(), SpellID(SpellID::HASTE).toSpell()->getIconIndex());
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, CreatureArtRequiresMatchingConvertedTargetAndPolicy)
{
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const auto wisp = CreatureID(CreatureID::decode("new-horizons:wisp"));
	const auto ballista = CreatureID(CreatureID::BALLISTA);
	for(const auto & row : std::array<std::tuple<const char *, CreatureID, const char *>, 2>{{
		{"core:monere", wisp, "newHorizons/heroSpecialties/MonereWispLine"},
		{"core:gerwulf", ballista, "newHorizons/heroSpecialties/GerwulfBallistaLine"}}})
	{
		const auto & [id, target, artwork] = row;
		SCOPED_TRACE(id);
		const auto & hero = prototype(id);
		const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, development, magic, target);
		ASSERT_TRUE(metadata.artwork);
		EXPECT_EQ(*metadata.artwork, artwork);
		EXPECT_EQ(metadata.creature, target);
		const auto view = heroSpecialtyPresentation(hero, metadata);
		EXPECT_EQ(view.animation().getOriginalName(), std::string(artwork) + "/44");
		EXPECT_EQ(view.animation(true).getOriginalName(), std::string(artwork) + "/32");
		EXPECT_EQ(view.frame(), 0u);
		auto disabled = development;
		disabled.Struct().erase("creatureLineSpecialties");
		EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, disabled, magic, target).artwork);
		EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, JsonNode(), magic, target).artwork);
		EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, development, magic, CreatureID(CreatureID::ARCHER)).artwork);
	}
	// A legacy Psychic Elemental Monere never inherits the selected Wisp image.
	EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(prototype("core:monere"),
		development, magic, std::nullopt).artwork);
	EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(prototype("core:pasis"),
		development, magic, wisp).artwork);
}
