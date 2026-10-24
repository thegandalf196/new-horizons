/*
 * NewHorizonsResourceSpecialtyArtTest.cpp, part of VCMI engine
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
#include "../../../lib/entities/hero/NewHorizonsSpecialtyArtwork.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/ResourceSet.h"
#include "../../../lib/modding/CModHandler.h"
#include <array>

namespace
{
struct ResourceArtCase
{
	const char * hero;
	GameResID resource;
	int configuredValue;
	const char * stem;
};

const std::array<ResourceArtCase, 13> resourceArtCases = {{
	{"core:caitlin", GameResID::GOLD, 350, "CaitlinGoldIncome"},
	{"core:clavius", GameResID::GOLD, 350, "ClaviusGoldIncome"},
	{"core:rissa", GameResID::MERCURY, 1, "RissaMercuryIncome"},
	{"core:calid", GameResID::SULFUR, 1, "CalidSulfurIncome"},
	{"core:saurug", GameResID::GEMS, 1, "SaurugGemsIncome"},
	{"core:sephinroth", GameResID::CRYSTAL, 1, "SephinrothCrystalIncome"},
	{"core:gelare", GameResID::GOLD, 350, "GelareGoldIncome"},
	{"core:grindan", GameResID::GOLD, 350, "GrindanGoldIncome"},
	{"core:aine", GameResID::GOLD, 350, "AineGoldIncome"},
	{"core:octavia", GameResID::GOLD, 350, "OctaviaGoldIncome"},
	{"core:nagash", GameResID::GOLD, 350, "NagashGoldIncome"},
	{"core:damacon", GameResID::GOLD, 350, "DamaconGoldIncome"},
	{"core:jenova", GameResID::GOLD, 350, "JenovaGoldIncome"}
}};

void expectResourceArt(const CHero & hero, const newHorizonsHeroes::SpecialtyPresentation & metadata,
	const ResourceArtCase & row)
{
	const std::string artwork = "newHorizons/heroSpecialties/" + std::string(row.stem);
	EXPECT_EQ(metadata.artwork, artwork);
	EXPECT_EQ(metadata.name, hero.getSpecialtyNameTranslated());
	EXPECT_EQ(metadata.description, hero.getSpecialtyDescriptionTranslated());
	EXPECT_FALSE(metadata.creature);
	EXPECT_FALSE(metadata.spell);
	EXPECT_FALSE(metadata.skill);
	const auto card = heroSpecialtyPresentation(hero, metadata);
	EXPECT_EQ(card.animation().getOriginalName(), artwork + "/44");
	EXPECT_EQ(card.animation(true).getOriginalName(), artwork + "/32");
	EXPECT_EQ(card.frame(), 0u);
}

void expectAtlasFallback(const CGHeroInstance & hero)
{
	const auto metadata = hero.getSpecialtyPresentation();
	EXPECT_FALSE(metadata.artwork);
	const auto card = heroSpecialtyPresentation(*hero.getHeroType(), metadata);
	EXPECT_EQ(card.animation().getOriginalName(), "UN44");
	EXPECT_EQ(card.animation(true).getOriginalName(), "UN32");
	EXPECT_EQ(card.frame(), hero.getHeroType()->imageIndex);
}
}

class NewHorizonsResourceSpecialtyArtTest : public HeroCommandFixture,
	public testing::WithParamInterface<size_t>
{
protected:
	bool historical = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(historical)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
	}

	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(resourceArtCases.at(GetParam()).hero)), PlayerColor(0))
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(1));
		startWithMap(std::move(builder));
	}
};

TEST_P(NewHorizonsResourceSpecialtyArtTest, CapturedAndPrototypeViewsKeepIncomeTextAndState)
{
	prepare();
	const auto & row = resourceArtCases.at(GetParam());
	SCOPED_TRACE(row.hero);
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	const auto beforeRules = hero->getPrimaryGrowthRules();
	const auto beforeBonuses = hero->getExportedBonusList().toJsonNode();
	const auto beforeIncome = hero->dailyIncome();
	const auto metadata = hero->getSpecialtyPresentation();
	expectResourceArt(*hero->getHeroType(), metadata, row);
	const auto preview = newHorizonsHeroes::resolveSpecialtyPresentation(*hero->getHeroType(),
		hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt);
	expectResourceArt(*hero->getHeroType(), preview, row);
	// Verify the actual configured specialty without asserting total income,
	// which can also include ordinary secondary Skills and other sources.
	int specialtyIncome = 0;
	for(const auto & bonus : hero->getExportedBonusList())
		if(bonus && bonus->type == BonusType::GENERATE_RESOURCE
			&& bonus->source == BonusSource::HERO_SPECIAL
			&& bonus->sid == BonusSourceID(hero->getHeroTypeID())
			&& bonus->subtype == BonusSubtypeID(row.resource))
			specialtyIncome += bonus->val;
	EXPECT_EQ(specialtyIncome, row.configuredValue);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), beforeRules);
	EXPECT_EQ(hero->getExportedBonusList().toJsonNode(), beforeBonuses);
	EXPECT_EQ(hero->dailyIncome(), beforeIncome);
}

TEST_P(NewHorizonsResourceSpecialtyArtTest, MissingOrMismatchedCapturedMechanicKeepsAtlasFallback)
{
	prepare();
	const auto & row = resourceArtCases.at(GetParam());
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	std::vector<std::shared_ptr<Bonus>> originals;
	for(const auto & bonus : hero->getExportedBonusList())
		if(bonus && bonus->type == BonusType::GENERATE_RESOURCE
			&& bonus->source == BonusSource::HERO_SPECIAL)
			originals.push_back(bonus);
	ASSERT_EQ(originals.size(), 1u);
	const auto original = originals.front();
	for(const auto & bonus : originals)
		hero->removeBonus(bonus);
	expectAtlasFallback(*hero);

	for(int mismatch = 0; mismatch < 5; ++mismatch)
	{
		SCOPED_TRACE(mismatch);
		auto changed = std::make_shared<Bonus>(*original);
		switch(mismatch)
		{
			case 0: changed->type = BonusType::LUCK; break;
			case 1: changed->subtype = BonusSubtypeID(GameResID(GameResID::WOOD)); break;
			case 2: changed->source = BonusSource::SECONDARY_SKILL; break;
			case 3: changed->sid = BonusSourceID(HeroTypeID(HeroTypeID::decode("core:christian"))); break;
			case 4: changed->subtype = BonusSubtypeID(PrimarySkill::ATTACK); break;
		}
		hero->addNewBonus(changed);
		expectAtlasFallback(*hero);
		hero->removeBonus(changed);
	}
	// The detached prototype still qualifies, but must never repair the live view.
	expectResourceArt(*hero->getHeroType(), newHorizonsHeroes::resolveSpecialtyPresentation(
		*hero->getHeroType(), hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt), row);
	hero->addNewBonus(original);
	expectResourceArt(*hero->getHeroType(), hero->getSpecialtyPresentation(), row);
}

TEST_P(NewHorizonsResourceSpecialtyArtTest, RetainedHistoricalResourceMechanicNeedsNoUnrelatedPolicy)
{
	historical = true;
	prepare();
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	EXPECT_TRUE(hero->getPrimaryGrowthRules().isNull());
	expectResourceArt(*hero->getHeroType(), hero->getSpecialtyPresentation(), resourceArtCases.at(GetParam()));
}

TEST_P(NewHorizonsResourceSpecialtyArtTest, ResourceLookupRequiresExactHeroFamilyAndTarget)
{
	using namespace newHorizonsHeroes::detail;
	const auto & row = resourceArtCases.at(GetParam());
	const auto target = GameResID::encode(row.resource.getNum());
	EXPECT_EQ(resolvedSpecialtyArtwork(row.hero, SpecialtyArtworkFamily::Resource, target),
		"newHorizons/heroSpecialties/" + std::string(row.stem));
	EXPECT_FALSE(resolvedSpecialtyArtwork(row.hero, SpecialtyArtworkFamily::Spell, target));
	EXPECT_FALSE(resolvedSpecialtyArtwork(row.hero, SpecialtyArtworkFamily::Resource, "wood"));
	EXPECT_FALSE(resolvedSpecialtyArtwork("core:christian", SpecialtyArtworkFamily::Resource, target));
}

INSTANTIATE_TEST_SUITE_P(SelectedResourceMechanics, NewHorizonsResourceSpecialtyArtTest,
	testing::Range(size_t{0}, resourceArtCases.size()));
