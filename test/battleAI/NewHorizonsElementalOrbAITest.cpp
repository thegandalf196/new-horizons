/*
 * NewHorizonsElementalOrbAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../spells/NewHorizonsMagicProfileFixture.h"
#include "../../AI/Nullkiller2/AIUtility.h"
#include "../../lib/GameConstants.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"

namespace
{
struct OrbCase
{
	const char * artifact;
	const char * spell;
	SpellDamageElement element;
};
}

class NewHorizonsElementalOrbAITest : public HeroCommandFixture,
	public ::testing::WithParamInterface<OrbCase>
{
protected:
	std::unique_ptr<newHorizonsTest::MagicV1Baseline> baseline;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
		baseline = std::make_unique<newHorizonsTest::MagicV1Baseline>();
	}

	void TearDown() override
	{
		HeroCommandFixture::TearDown();
		baseline.reset();
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
};

TEST_P(NewHorizonsElementalOrbAITest, MatchingCastableSpellValuesOrbAboveWeakerEquipment)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->removeAllSpells();
	for(const auto skill : newHorizonsMagic::schoolSkills(attackerSideHero->getMagicRules()))
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto & entry = GetParam();
	const SpellID spellID(SpellID::decode(entry.spell));
	ASSERT_EQ(spellID.toSpell()->getDamageElement(), entry.element);
	attackerSideHero->addSpellToSpellbook(spellID);
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(spellID.toSpell()));
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode(entry.artifact)), ArtifactPosition::BACKPACK_START);
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode("core:cloverOfFortune")), ArtifactPosition::MISC1);
	const auto * orb = attackerSideHero->getArt(ArtifactPosition::BACKPACK_START);
	const auto * incumbent = attackerSideHero->getArt(ArtifactPosition::MISC1);
	ASSERT_NE(orb, nullptr);
	ASSERT_NE(incumbent, nullptr);
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(attackerSideHero, orb), 25 * 120);
	EXPECT_GT(NK2AI::getArtifactScoreForHero(attackerSideHero, orb),
		NK2AI::getArtifactScoreForHero(attackerSideHero, incumbent));
	gameState()->getMap().allowedSpells.erase(spellID);
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(attackerSideHero, orb), 0);
}

TEST_P(NewHorizonsElementalOrbAITest, UnrelatedElementAndUntaggedMagicDoNotSupplyRelevance)
{
	startGame();
	attackerSideHero->removeAllSpells();
	const auto & entry = GetParam();
	const SpellID unrelated(SpellID::decode(entry.element == SpellDamageElement::FIRE
		? "core:iceBolt" : "core:fireball"));
	attackerSideHero->addSpellToSpellbook(unrelated);
	giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode(entry.artifact)), ArtifactPosition::BACKPACK_START);
	const auto * orb = attackerSideHero->getArt(ArtifactPosition::BACKPACK_START);
	ASSERT_NE(orb, nullptr);
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(attackerSideHero, orb), 0);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	ASSERT_NE(unrelated.toSpell()->getDamageElement(), entry.element);
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(unrelated.toSpell()));
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(attackerSideHero, orb), 0);
	attackerSideHero->removeAllSpells();
	const SpellID untagged(SpellID::decode("core:magicArrow"));
	ASSERT_EQ(untagged.toSpell()->getDamageElement(), SpellDamageElement::NONE);
	attackerSideHero->addSpellToSpellbook(untagged);
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(attackerSideHero, orb), 0);
}

INSTANTIATE_TEST_SUITE_P(FourElements, NewHorizonsElementalOrbAITest, ::testing::Values(
	OrbCase{"core:orbOfTheFirmament", "core:lightningBolt", SpellDamageElement::AIR},
	OrbCase{"core:orbOfTempestuousFire", "core:fireball", SpellDamageElement::FIRE},
	OrbCase{"core:orbOfDrivingRain", "core:iceBolt", SpellDamageElement::WATER},
	OrbCase{"core:orbOfSilt", "core:meteorShower", SpellDamageElement::EARTH}));

TEST(NewHorizonsElementalOrbAIScoreTest, LegacySpellDamageKeepsExistingScoreWithoutPriceFallback)
{
	CArtifact artifact;
	artifact.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE, BonusSource::ARTIFACT, 50, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	EXPECT_EQ(NK2AI::getPotentialArtifactScore(&artifact), 50 * 120);
}
