/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/entities/hero/NewHorizonsPariahLearning.h"
#include "../../../lib/entities/hero/NewHorizonsSchoolSpellcraft.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto ATTACKER = BattleSide::ATTACKER;
constexpr auto DEFENDER = BattleSide::DEFENDER;

SpellID spell(const char * key)
{
	return SpellID(SpellID::decode(key));
}

std::set<SpellID> levelSpells(const CGHeroInstance & hero, const char * school, int level)
{
	std::set<SpellID> result;
	const SpellSchool wanted(SpellSchool::decode(school));
	for(const auto & candidate : LIBRARY->spellh->objects)
	{
		if(!candidate || !newHorizonsMagic::spellAllowedByHeroRoster(hero.getMagicRules(), candidate->getId())
			|| newHorizonsMagic::spellLevel(hero.getMagicRules(), candidate->getId()) != level)
			continue;
		const auto schools = newHorizonsMagic::spellSchools(hero.getMagicRules(), candidate->getId());
		if(std::ranges::find(schools, wanted) != schools.end())
			result.insert(candidate->getId());
	}
	return result;
}

class CastlePariahTest : public CastleSpecialtyTest
{
protected:
	std::set<SpellID> learnedShadow(const std::set<SpellID> & before) const
	{
		std::set<SpellID> result;
		for(const auto & known : attackerSideHero->getSpellsInSpellbook())
			if(!before.contains(known))
				result.insert(known);
		return result;
	}

};
}

TEST_F(CastlePariahTest, PariahLowersTheWholeArmysMorale)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:cuthbert"));
	newHorizonsHeroes::installCastleSpecialtyBonuses(*attackerSideHero);
	newHorizonsHeroes::installCastleSpecialtyBonuses(*attackerSideHero); // Replaces, never stacks.
	const auto pariah = Selector::type()(BonusType::MORALE).And(Selector::sourceType()(BonusSource::HERO_SPECIAL));
	EXPECT_EQ(attackerSideHero->valOfBonuses(pariah), -2);
	ASSERT_NO_FATAL_FAILURE(arena());
	EXPECT_EQ(waiter->valOfBonuses(pariah), -2);
	EXPECT_EQ(enemy->valOfBonuses(pariah), 0);
}

TEST_F(CastlePariahTest, PariahMultipliesOnlyShadowSpellPowerTerms)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:cuthbert", 8));
	const auto & magic = attackerSideHero->getMagicRules();
	const auto shadow = newHorizonsHeroes::captureSpellcraftContext(magic, attackerSideHero, spell("new-horizons:lifeDrain"), true);
	ASSERT_TRUE(shadow.factors);
	EXPECT_EQ(shadow.factors->enhancedEfficiencyBasisPoints, shadow.factors->ordinaryEfficiencyPercent * 140);
	EXPECT_EQ(shadow.factors->scope, newHorizonsHeroes::SpellcraftSpecialtyScope::NUMERICAL);
	EXPECT_FALSE(newHorizonsHeroes::captureSpellcraftContext(magic, attackerSideHero, SpellID(SpellID::BLESS), true).factors);
	attackerSideHero->level = 30;
	const auto capped = newHorizonsHeroes::captureSpellcraftContext(magic, attackerSideHero, spell("new-horizons:lifeDrain"), true);
	ASSERT_TRUE(capped.factors);
	EXPECT_EQ(capped.factors->enhancedEfficiencyBasisPoints, capped.factors->ordinaryEfficiencyPercent * 200);
}

TEST_F(CastlePariahTest, PariahLearnsAMirroredShadowSpellForEachLightSpell)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:cuthbert"));
	attackerSideHero->removeAllSpells();
	const auto shadowTwo = levelSpells(*attackerSideHero, "new-horizons:shadow", 2);
	ASSERT_FALSE(shadowTwo.empty());
	gameHandler->changeSpells(attackerSideHero, true, {spell("new-horizons:holyArmor")});
	const auto learned = learnedShadow({spell("new-horizons:holyArmor")});
	ASSERT_EQ(learned.size(), 1u);
	EXPECT_TRUE(shadowTwo.contains(*learned.begin()));
	// Knowing every level 2 Shadow spell leaves nothing to mirror.
	for(const auto & known : shadowTwo)
		attackerSideHero->addSpellToSpellbook(known);
	const auto before = attackerSideHero->getSpellsInSpellbook();
	gameHandler->changeSpells(attackerSideHero, true, {spell("new-horizons:guardianSpirit")});
	EXPECT_EQ(learnedShadow(before), std::set<SpellID>{spell("new-horizons:guardianSpirit")});
	// A non-Light spell mirrors nothing.
	const auto beforeShadow = attackerSideHero->getSpellsInSpellbook();
	gameHandler->changeSpells(attackerSideHero, true, {spell("core:curse")});
	EXPECT_EQ(learnedShadow(beforeShadow), std::set<SpellID>{spell("core:curse")});
}

TEST_F(CastlePariahTest, PariahMirrorsStartingLightSpells)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:cuthbert"));
	attackerSideHero->removeAllSpells();
	attackerSideHero->addSpellToSpellbook(spell("new-horizons:holyArmor"));
	CRandomGenerator rng(7);
	const auto mirrors = newHorizonsHeroes::pariahMirrorSpells(*attackerSideHero,
		attackerSideHero->getSpellsInSpellbook(), rng, true);
	ASSERT_EQ(mirrors.size(), 1u);
	EXPECT_TRUE(levelSpells(*attackerSideHero, "new-horizons:shadow", 2).contains(mirrors.front()));
}

TEST_F(CastlePariahTest, HistoricalCuthbertLearnsOnlyWhatHeIsTaught)
{
	stripped = {"pariah"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:cuthbert", 8));
	attackerSideHero->removeAllSpells();
	gameHandler->changeSpells(attackerSideHero, true, {spell("new-horizons:holyArmor")});
	EXPECT_EQ(attackerSideHero->getSpellsInSpellbook(), std::set<SpellID>{spell("new-horizons:holyArmor")});
	EXPECT_FALSE(newHorizonsHeroes::captureSpellcraftContext(attackerSideHero->getMagicRules(), attackerSideHero,
		spell("new-horizons:lifeDrain"), true).factors);
}
