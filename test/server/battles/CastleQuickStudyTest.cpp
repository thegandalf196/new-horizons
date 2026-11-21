/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../lib/battle/QuickStudySpecialty.h"
#include "../../../lib/battle/WisdomCombatState.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto ATTACKER = BattleSide::ATTACKER;
constexpr auto DEFENDER = BattleSide::DEFENDER;

class CastleQuickStudyTest : public CastleSpecialtyTest
{
protected:
};
}

TEST_F(CastleQuickStudyTest, QuickStudyCopiesASeenSpellOnceAtReducedPower)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:sanya"));
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto bless = SpellID(SpellID::BLESS);
	ASSERT_FALSE(attackerSideHero->canCastThisSpell(bless.toSpell()));
	EXPECT_FALSE(quickStudySpecialty::canCopy(*battle(), *attackerSideHero, bless)) << "Not seen yet";
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(submit(spellAt(DEFENDER, bless, enemy), PlayerColor(1)));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(quickStudySpecialty::canCopy(*battle(), *attackerSideHero, bless));
	spells::BattleCast copy(battle(), attackerSideHero, spells::Mode::HERO, bless.toSpell());
	const auto mechanics = bless.toSpell()->battleMechanics(&copy);
	EXPECT_EQ(mechanics->getEffectPower(), attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER) * 525 / 1000);
	ASSERT_TRUE(submit(spellAt(ATTACKER, bless, actor)));
	EXPECT_TRUE(actor->hasBonusFrom(BonusSource::SPELL_EFFECT, BonusSourceID(bless)));
	EXPECT_EQ(quickStudySpecialty::spentCopies(*battle(), *attackerSideHero), 1);
	EXPECT_FALSE(quickStudySpecialty::canCopy(*battle(), *attackerSideHero, bless)) << "Once per spell";
}

TEST_F(CastleQuickStudyTest, QuickStudyBudgetIsOneThenTwoFromLevelTenAndSurvivesSaving)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:sanya"));
	EXPECT_EQ(newHorizonsHeroes::quickStudyUses(attackerSideHero), 1);
	attackerSideHero->level = 10;
	EXPECT_EQ(newHorizonsHeroes::quickStudyUses(attackerSideHero), 2);
	EXPECT_EQ(newHorizonsHeroes::quickStudySpellPowerPermille(attackerSideHero), 750);
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(submit(spellAt(DEFENDER, SpellID::BLESS, enemy), PlayerColor(1)));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::BLESS, actor)));
	// The copy budget is derived from the serialized completed cast history.
	CMemorySerializer memory;
	memory.oser & battle()->getSide(ATTACKER).wisdomCombatState;
	WisdomCombatState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.completedCasts, battle()->getSide(ATTACKER).wisdomCombatState.completedCasts);
	EXPECT_EQ(quickStudySpecialty::spentCopies(*battle(), *attackerSideHero), 1);
	attackerSideHero->level = 9;
	EXPECT_FALSE(quickStudySpecialty::canCopy(*battle(), *attackerSideHero, SpellID(SpellID::BLESS)));
}

TEST_F(CastleQuickStudyTest, HistoricalSanyaCannotCopySpells)
{
	stripped = {"quickStudy"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:sanya", 20));
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(submit(spellAt(DEFENDER, SpellID::BLESS, enemy), PlayerColor(1)));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	EXPECT_FALSE(quickStudySpecialty::canCopy(*battle(), *attackerSideHero, SpellID(SpellID::BLESS)));
	EXPECT_FALSE(submit(spellAt(ATTACKER, SpellID::BLESS, actor)));
}
