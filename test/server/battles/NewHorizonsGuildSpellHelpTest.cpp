/*
 * NewHorizonsGuildSpellHelpTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../client/windows/NewHorizonsGuildSpellHelp.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/texts/CGeneralTextHandler.h"

#include <array>
#include <tuple>

class NewHorizonsGuildSpellHelpTest : public NewHorizonsElementalTerrainFixture
{
protected:
	SpellID named(const char * key) const { return SpellID(SpellID::decode(key)); }
	std::string help(SpellID spell, const CGHeroInstance * visitor) const
	{
		return newHorizonsGuildSpellHelp::acquisitionRequirement(
			attackerSideHero->getMagicRules(), spell.toSpell(), visitor)
			.toString(LIBRARY->generaltexth.get());
	}
	void prepare(SpellID spell)
	{
		startGame();
		gameState()->getMap().allowedSpells.insert(spell);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeSpellFromSpellbook(spell);
		for(const auto skill : newHorizonsMagic::schoolSkills(attackerSideHero->getMagicRules()))
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	}
	std::string required(int rank, const char * skillName) const
	{
		return LIBRARY->generaltexth->translate("new-horizons.adventure.spellLearning.requires")
			+ LIBRARY->generaltexth->translate("core.skilllev", rank - 1)
			+ LIBRARY->generaltexth->translate("new-horizons.adventure.spellLearning.proficiencyIn")
			+ LIBRARY->skillh->getById(SecondarySkill(SecondarySkill::decode(skillName)))->getNameTranslated();
	}
};

TEST_F(NewHorizonsGuildSpellHelpTest, UnknownLevelThreeFourFiveShowExactAcquisitionRanks)
{
	const auto focus = named("new-horizons:focusMagic");
	ASSERT_NO_FATAL_FAILURE(prepare(focus));
	for(const auto & [spell, rank, school] : std::array<std::tuple<SpellID, int, const char *>, 3>{{
		{focus, 1, "new-horizons:sorceryMagic"},
		{named("core:meteorShower"), 2, "new-horizons:havocMagic"},
		{named("core:armageddon"), 3, "new-horizons:havocMagic"}}})
	{
		gameState()->getMap().allowedSpells.insert(spell);
		attackerSideHero->removeSpellFromSpellbook(spell);
		ASSERT_EQ(attackerSideHero->getSpellLearningStatus(spell.toSpell()),
			CGHeroInstance::SpellLearningStatus::INSUFFICIENT_SCHOOL);
		EXPECT_EQ(help(spell, attackerSideHero), required(rank, school));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell));
	}
}

TEST_F(NewHorizonsGuildSpellHelpTest, EligibleUnknownSpellStillShowsRankWithoutChangingAcquisition)
{
	const auto spell = named("core:meteorShower");
	ASSERT_NO_FATAL_FAILURE(prepare(spell));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getSpellLearningStatus(spell.toSpell()), CGHeroInstance::SpellLearningStatus::LEARNABLE);
	EXPECT_EQ(help(spell, attackerSideHero), required(2, "new-horizons:havocMagic"));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell));
}

TEST_F(NewHorizonsGuildSpellHelpTest, InscribedSpellBelowSchoolRankIsNotPresentedAsLocked)
{
	const auto spell = named("core:meteorShower");
	ASSERT_NO_FATAL_FAILURE(prepare(spell));
	attackerSideHero->addSpellToSpellbook(spell);
	ASSERT_FALSE(newHorizonsMagic::hasSchoolProficiency(attackerSideHero, spell));
	EXPECT_TRUE(help(spell, attackerSideHero).empty());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell));
}

TEST_F(NewHorizonsGuildSpellHelpTest, NoVisitorGetsGenericRankInformationNotAnotherHeroesStatus)
{
	const auto spell = named("core:meteorShower");
	ASSERT_NO_FATAL_FAILURE(prepare(spell));
	attackerSideHero->addSpellToSpellbook(spell);
	EXPECT_TRUE(help(spell, attackerSideHero).empty());
	EXPECT_EQ(help(spell, nullptr), required(2, "new-horizons:havocMagic"));
}

TEST_F(NewHorizonsGuildSpellHelpTest, MissingBookAndBannedSpellsAreNotMisdiagnosedAsSchoolLocks)
{
	const auto spell = named("core:meteorShower");
	ASSERT_NO_FATAL_FAILURE(prepare(spell));
	attackerSideHero->removeSpellbook();
	ASSERT_EQ(attackerSideHero->getSpellLearningStatus(spell.toSpell()), CGHeroInstance::SpellLearningStatus::UNAVAILABLE);
	EXPECT_TRUE(help(spell, attackerSideHero).empty());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	gameState()->getMap().allowedSpells.erase(spell);
	ASSERT_EQ(attackerSideHero->getSpellLearningStatus(spell.toSpell()), CGHeroInstance::SpellLearningStatus::UNAVAILABLE);
	EXPECT_TRUE(help(spell, attackerSideHero).empty());
}

TEST_F(NewHorizonsGuildSpellHelpTest, LowerLevelsLegacyAndNullSpellDoNotGainNewRankHelp)
{
	const auto spell = named("core:fireball");
	ASSERT_NO_FATAL_FAILURE(prepare(spell));
	EXPECT_TRUE(help(spell, nullptr).empty());
	EXPECT_TRUE(newHorizonsGuildSpellHelp::acquisitionRequirement(JsonNode(), spell.toSpell(), nullptr).empty());
	EXPECT_TRUE(newHorizonsGuildSpellHelp::acquisitionRequirement(attackerSideHero->getMagicRules(), nullptr, nullptr).empty());
}

TEST_F(NewHorizonsGuildSpellHelpTest, MultipleSavedSchoolsRemainAnAlternativeNotAnAllSchoolsRequirement)
{
	const auto spell = named("core:meteorShower");
	ASSERT_NO_FATAL_FAILURE(prepare(spell));
	auto rules = attackerSideHero->getMagicRules();
	auto & schools = rules["spells"]["core:meteorShower"]["schools"].Vector();
	schools.emplace_back(std::string("new-horizons:sorcery"));
	const auto text = newHorizonsGuildSpellHelp::acquisitionRequirement(rules, spell.toSpell(), nullptr)
		.toString(LIBRARY->generaltexth.get());
	EXPECT_EQ(text, required(2, "new-horizons:havocMagic")
		+ LIBRARY->generaltexth->translate("new-horizons.adventure.spellLearning.or")
		+ LIBRARY->skillh->getById(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")))->getNameTranslated());
}

TEST_F(NewHorizonsGuildSpellHelpTest, VisitorCapturedRulesWinOverUnrelatedWorldPresentationContext)
{
	const auto spell = named("core:meteorShower");
	ASSERT_NO_FATAL_FAILURE(prepare(spell));
	const auto text = newHorizonsGuildSpellHelp::acquisitionRequirement(JsonNode(), spell.toSpell(), attackerSideHero)
		.toString(LIBRARY->generaltexth.get());
	EXPECT_EQ(text, required(2, "new-horizons:havocMagic"));
}
