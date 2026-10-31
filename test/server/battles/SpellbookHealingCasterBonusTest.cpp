/* SpellbookHealingCasterBonusTest.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/SpellEffectPreview.h"
#include "../../../lib/bonuses/Bonus.h"
namespace
{
class SpellbookHealingCasterBonusTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare(bool orb, const char * artifact = "core:orbOfDrivingRain")
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
		for(const auto & [school, skill] : attackerSideHero->getMagicRules()["schoolSkills"].Struct())
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill.String())), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,0,ChangeValueMode::ABSOLUTE);
		if(!attackerSideHero->hasSpellbook())
			giveArtifact(attackerSideHero,ArtifactID::SPELLBOOK,ArtifactPosition::SPELLBOOK);
		if(orb)
			giveArtifact(attackerSideHero,ArtifactID(ArtifactID::decode(artifact)),ArtifactPosition::MISC1);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		attackerSideHero->addSpellToSpellbook(SpellID::RESURRECTION);
		startBattle();
	}
	void compareHealing(SpellID id,int64_t expected)
	{
		const auto * spell = id.toSpell();
		const auto mana = attackerSideHero->getManaAvailable();
		const auto known = attackerSideHero->getSpellsInSpellbook();
		spells::BattleCast event(battle(),attackerSideHero,spells::Mode::HERO,spell);
		const auto mechanics = spell->battleMechanics(&event);
		const auto authoritative = mechanics->applySpellBonus(mechanics->getRecipientEffectValue(nullptr),nullptr);
		const auto row = newHorizonsMagic::spellEffectPreview(*attackerSideHero,*spell,battle());
		ASSERT_TRUE(row.primary);
		EXPECT_EQ(authoritative,expected);
		EXPECT_EQ(row.primary->value,authoritative);
		EXPECT_EQ(attackerSideHero->getManaAvailable(),mana);
		EXPECT_EQ(attackerSideHero->getSpellsInSpellbook(),known);
	}
};
}
TEST_F(SpellbookHealingCasterBonusTest, OrdinaryCureUsesItsActualCasterHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	compareHealing(SpellID::CURE,25);
}
TEST_F(SpellbookHealingCasterBonusTest, CurrentDrivingRainDoesNotBoostHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	compareHealing(SpellID::CURE,25);
}
TEST_F(SpellbookHealingCasterBonusTest, CurrentSiltDoesNotBoostRestoration)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true,"core:orbOfSilt"));
	compareHealing(SpellID::RESURRECTION,100);
}

TEST_F(SpellbookHealingCasterBonusTest, SpecificCasterBonusFinishesCureBeforeDisplay)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	// A supported explicit caster bonus, unlike the current damage-only elemental Orbs.
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPECIFIC_SPELL_DAMAGE, BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::CURE))));
	compareHealing(SpellID::CURE,37);
}
TEST_F(SpellbookHealingCasterBonusTest, SpecificCasterBonusFinishesResurrectionBeforeDisplay)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPECIFIC_SPELL_DAMAGE, BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::RESURRECTION))));
	compareHealing(SpellID::RESURRECTION,150);
}
