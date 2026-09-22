/*
 * NewHorizonsHalonInitializationTest.cpp, part of VCMI
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

#include "../../lib/GameConstants.h"
#include "../../lib/bonuses/BonusEnum.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace
{
SecondarySkill scopedSkill(const char * identifier)
{
	return SecondarySkill(SecondarySkill::decode(identifier));
}
}

class NewHorizonsHalonInitializationTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
};

TEST_F(NewHorizonsHalonInitializationTest, FreshHalonUsesMetamagicAndSpellcraft)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:halon")), PlayerColor(0));
	startWithMap(std::move(builder));

	const auto * halon = findHeroAt({5, 5, 0});
	ASSERT_NE(halon, nullptr);

	const auto metamagic = scopedSkill("new-horizons:metamagic");
	const auto spellcraft = scopedSkill("new-horizons:spellcraft");
	ASSERT_EQ(halon->secSkills.size(), 2u);
	EXPECT_EQ(halon->getSecSkillLevel(metamagic), MasteryLevel::BASIC);
	EXPECT_EQ(halon->getSecSkillLevel(spellcraft), MasteryLevel::BASIC);
	EXPECT_EQ(halon->getSecSkillLevel(SecondarySkill::WISDOM), MasteryLevel::NONE);
	EXPECT_EQ(halon->getSecSkillLevel(SecondarySkill::MYSTICISM), MasteryLevel::NONE);

	// Basic Metamagic supplies one use and Halon's converted specialty supplies
	// the additional use described by the hero text. Basic Spellcraft also owns
	// the former Mysticism regeneration role.
	EXPECT_EQ(halon->valOfBonuses(BonusType::METAMAGIC_USES_PER_COMBAT), 2);
	EXPECT_EQ(halon->valOfBonuses(BonusType::MANA_REGENERATION), 1);
	EXPECT_EQ(halon->getHeroType()->getSpecialtyNameTranslated(), "Metamagic Adept");
	EXPECT_EQ(halon->getHeroType()->getSpecialtyDescriptionTranslated(),
		"Halon can use Metamagic one additional time per combat.");
}

TEST_F(NewHorizonsHalonInitializationTest, FreshBrissaKeepsHasteAndStartsWithinLeadershipLimits)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:brissa")), PlayerColor(0));
	startWithMap(std::move(builder));

	const auto * brissa = findHeroAt({5, 5, 0});
	ASSERT_NE(brissa, nullptr);
	EXPECT_TRUE(brissa->spellbookContainsSpell(SpellID(SpellID::HASTE)));
	EXPECT_EQ(brissa->getSecSkillLevel(scopedSkill("new-horizons:elementalRebirth")), MasteryLevel::BASIC);
	EXPECT_EQ(brissa->getSecSkillLevel(scopedSkill("new-horizons:sorceryMagic")), MasteryLevel::BASIC);
	EXPECT_EQ(brissa->getSecSkillLevel(SecondarySkill::WISDOM), MasteryLevel::NONE);
	EXPECT_EQ(brissa->getSecSkillLevel(SecondarySkill::AIR_MAGIC), MasteryLevel::NONE);

	for(const auto & [slot, stack] : brissa->Slots())
	{
		(void)slot;
		const auto capacity = brissa->getLeadershipSlotCapacity(stack->getCreatureID());
		ASSERT_TRUE(capacity) << stack->getType()->getJsonKey();
		EXPECT_LE(stack->getCount(), capacity->maximum) << stack->getType()->getJsonKey();
	}
}

TEST_F(NewHorizonsHalonInitializationTest, FreshSolmyrUsesMasterChainLightningAndStormcaller)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(0));
	startWithMap(std::move(builder));

	const auto * solmyr = findHeroAt({5, 5, 0});
	ASSERT_NE(solmyr, nullptr);
	EXPECT_EQ(solmyr->getHeroClass()->getJsonKey(), "core:wizard");

	const auto metamagic = scopedSkill("new-horizons:metamagic");
	const auto havoc = scopedSkill("new-horizons:havocMagic");
	ASSERT_EQ(solmyr->secSkills.size(), 2u);
	EXPECT_EQ(solmyr->getSecSkillLevel(metamagic), MasteryLevel::BASIC);
	EXPECT_EQ(solmyr->getSecSkillLevel(havoc), MasteryLevel::BASIC);
	EXPECT_EQ(solmyr->getSecSkillLevel(SecondarySkill::WISDOM), MasteryLevel::NONE);
	EXPECT_EQ(solmyr->getSecSkillLevel(SecondarySkill::SORCERY), MasteryLevel::NONE);

	const SpellID masterChainLightning = SpellID::decode("new-horizons:masterChainLightning");
	const SpellID regularChainLightning(SpellID::CHAIN_LIGHTNING);
	ASSERT_TRUE(masterChainLightning.hasValue());
	EXPECT_TRUE(solmyr->spellbookContainsSpell(masterChainLightning));
	EXPECT_FALSE(solmyr->spellbookContainsSpell(regularChainLightning));
	EXPECT_TRUE(solmyr->canCastThisSpell(masterChainLightning.toSpell()));
	EXPECT_FALSE(solmyr->canCastThisSpell(regularChainLightning.toSpell()));
	EXPECT_TRUE(solmyr->getSourcesForSpell(masterChainLightning).size() > 0);
	EXPECT_TRUE(solmyr->getSourcesForSpell(regularChainLightning).empty());
	EXPECT_FALSE(solmyr->canLearnSpell(regularChainLightning.toSpell(), true));

	EXPECT_TRUE(solmyr->hasActivePerk("new-horizons:havocMagic",
		"new-horizons:havocMagic.stormcaller"));
	EXPECT_EQ(solmyr->valOfBonuses(BonusType::SPECIAL_SPELL_SCALING,
		BonusSubtypeID(masterChainLightning)), 0);
	ASSERT_EQ(solmyr->getPerkState().selected.size(), 1u);
	EXPECT_EQ(solmyr->getPerkState().selected.front().perkId,
		"new-horizons:havocMagic.stormcaller");
	EXPECT_EQ(solmyr->getHeroType()->getSpecialtyNameTranslated(), "Master Chain Lightning");
	EXPECT_EQ(solmyr->getHeroType()->getSpecialtyTooltipTranslated(), "Master Chain Lightning");

	const auto & rules = solmyr->getMagicRules();
	EXPECT_EQ(newHorizonsMagic::spellCost(rules, masterChainLightning, 0),
		newHorizonsMagic::spellCost(rules, regularChainLightning, 0));
	const auto masterFormula = newHorizonsMagic::spellDirectDamage(
		rules, masterChainLightning.toSpell()->getJsonKey());
	const auto regularFormula = newHorizonsMagic::spellDirectDamage(
		rules, regularChainLightning.toSpell()->getJsonKey());
	ASSERT_TRUE(masterFormula);
	ASSERT_TRUE(regularFormula);
	EXPECT_EQ(*masterFormula, *regularFormula);
}

TEST_F(NewHorizonsHalonInitializationTest, FormerAlchemistClassIsPresentedAsBattleMage)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:fafner")), PlayerColor(0));
	startWithMap(std::move(builder));

	const auto * fafner = findHeroAt({5, 5, 0});
	ASSERT_NE(fafner, nullptr);
	EXPECT_EQ(fafner->getHeroClass()->getJsonKey(), "core:alchemist");
	EXPECT_EQ(fafner->getHeroClass()->getNameTranslated(), "Battle Mage");
}

TEST_F(NewHorizonsHalonInitializationTest, MasterChainLightningDescriptionTracksCurrentHeroLevel)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(0));
	startWithMap(std::move(builder));

	auto * solmyr = findHeroAt({5, 5, 0});
	ASSERT_NE(solmyr, nullptr);
	const SpellID masterChainLightning = SpellID::decode("new-horizons:masterChainLightning");
	ASSERT_TRUE(masterChainLightning.hasValue());

	solmyr->level = 1;
	EXPECT_EQ(newHorizonsMagic::masterChainLightningRetentionPercent(solmyr->level), 76);
	const auto levelOne = newHorizonsMagic::spellDescriptionForHero(
		solmyr, masterChainLightning.toSpell(), 0);
	EXPECT_NE(levelOne.find("Current retention: 76%"), std::string::npos);

	solmyr->level = 12;
	EXPECT_EQ(newHorizonsMagic::masterChainLightningRetentionPercent(solmyr->level), 87);
	const auto levelTwelve = newHorizonsMagic::spellDescriptionForHero(
		solmyr, masterChainLightning.toSpell(), 0);
	EXPECT_NE(levelTwelve.find("Current retention: 87%"), std::string::npos);
	EXPECT_EQ(levelTwelve.find("Current retention: 76%"), std::string::npos);

	solmyr->level = 99;
	EXPECT_EQ(newHorizonsMagic::masterChainLightningRetentionPercent(solmyr->level), 90);
	const auto capped = newHorizonsMagic::spellDescriptionForHero(
		solmyr, masterChainLightning.toSpell(), 0);
	EXPECT_NE(capped.find("Current retention: 90%"), std::string::npos);
	EXPECT_EQ(capped.find("Current retention: 176%"), std::string::npos);
}
