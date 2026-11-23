/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../lib/battle/NewHorizonsFrostMantle.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto ATTACKER = BattleSide::ATTACKER;
constexpr auto DEFENDER = BattleSide::DEFENDER;

class CastleFrostMantleTest : public CastleSpecialtyTest
{
protected:
	bool cloaked(const battle::Unit & unit) const
	{
		return unit.hasBonus(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER).And(CSelector([](const Bonus * bonus)
			{ return bonus->stacking == newHorizonsFrostMantle::CLOAK_STACKING; })));
	}

	int chill(const battle::Unit & unit) const
	{
		return unit.valOfBonuses(Selector::type()(BonusType::STACKS_INITIATIVE).And(CSelector([](const Bonus * bonus)
			{ return bonus->stacking == newHorizonsFrostMantle::CHILL_STACKING; })));
	}

	/// No stack stands next to another, so a ring around either one reaches no stack.
	void isolated()
	{
		ASSERT_NO_FATAL_FAILURE(emptyField());
		active = place(ATTACKER, "core:pikeman", BattleHex(1, 1), 1, 1000);
		actor = place(ATTACKER, "core:crusader", BattleHex(5, 5), 20, 800);
		enemy = place(DEFENDER, "core:halberdier", BattleHex(12, 5), 30, 400);
		ASSERT_TRUE(active && actor && enemy);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_NO_FATAL_FAILURE(activate(active));
	}

	/// Makes the stack immune to Frost Ring by the given bonus for the whole battle.
	void immune(const CStack * unit, BonusType type, int value, BonusSubtypeID subtype = BonusSubtypeID())
	{
		effect(unit, Bonus(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, value, BonusSourceID(), subtype));
	}

	/// The hex-only legality query the battle interface asks while hovering.
	bool castableAt(BattleHex hex) const
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::FROST_RING).toSpell());
		return cast.getSpell()->battleMechanics(&cast)->canBeCastAt(spells::Target{spells::Destination(hex)});
	}
};
}

TEST_F(CastleFrostMantleTest, FrostMantleCloaksAFriendlyCentreAndReturnsColdOnMelee)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide", 10));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_TRUE(submit(spellAtHex(ATTACKER, SpellID::FROST_RING, actor->getPosition())));
	ASSERT_TRUE(cloaked(*actor));
	const auto cloak = actor->getBonus(CSelector([](const Bonus * bonus)
		{ return bonus->stacking == newHorizonsFrostMantle::CLOAK_STACKING; }));
	ASSERT_TRUE(cloak);
	EXPECT_EQ(cloak->turnsRemain, 2);
	EXPECT_EQ(cloak->val, 19) << "10% at level 1, +1% per further level";
	blockRetaliation(enemy); // The cloaked Angels never retaliate, so only cold damage returns.
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	const auto health = enemy->getAvailableHealth();
	ASSERT_TRUE(submit(BattleAction::makeMeleeAttack(enemy, actor, enemy->getPosition()), PlayerColor(1)));
	EXPECT_LT(enemy->getAvailableHealth(), health) << "Part of the hit comes back as cold damage";
	EXPECT_EQ(chill(*enemy), -20);
}

TEST_F(CastleFrostMantleTest, FrostMantleOnlyChillsAnEnemyCentre)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_TRUE(submit(spellAtHex(ATTACKER, SpellID::FROST_RING, enemy->getPosition())));
	EXPECT_FALSE(cloaked(*enemy));
	EXPECT_EQ(chill(*enemy), -20);
}

TEST_F(CastleFrostMantleTest, HistoricalAdelaideCastsAnOrdinaryFrostRing)
{
	stripped = {"frostMantle"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_TRUE(submit(spellAtHex(ATTACKER, SpellID::FROST_RING, actor->getPosition())));
	EXPECT_FALSE(cloaked(*actor));
}

TEST_F(CastleFrostMantleTest, FrostMantleRingMayBeCentredOnAnIsolatedFriendlyStack)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(isolated());
	EXPECT_TRUE(castableAt(actor->getPosition())) << "Hovering the stack offers the cloak";
	EXPECT_FALSE(castableAt(BattleHex(9, 1))) << "An empty ring around an empty hex stays illegal";
	const auto health = actor->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(submit(spellAtHex(ATTACKER, SpellID::FROST_RING, actor->getPosition())));
	EXPECT_TRUE(cloaked(*actor));
	EXPECT_EQ(actor->getAvailableHealth(), health) << "The ring never strikes its centre";
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(CastleFrostMantleTest, FrostMantleRingMayBeCentredOnAnIsolatedEnemyToChillIt)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(isolated());
	EXPECT_TRUE(castableAt(enemy->getPosition()));
	const auto health = enemy->getAvailableHealth();
	ASSERT_TRUE(submit(spellAtHex(ATTACKER, SpellID::FROST_RING, enemy->getPosition())));
	EXPECT_FALSE(cloaked(*enemy));
	EXPECT_EQ(chill(*enemy), -20);
	EXPECT_EQ(enemy->getAvailableHealth(), health);
}

TEST_F(CastleFrostMantleTest, OtherHeroesStillCannotRingAnIsolatedStack)
{
	stripped = {"frostMantle"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(isolated());
	EXPECT_FALSE(castableAt(actor->getPosition()));
	EXPECT_FALSE(castableAt(enemy->getPosition()));
	EXPECT_FALSE(submit(spellAtHex(ATTACKER, SpellID::FROST_RING, actor->getPosition())));
}

TEST_F(CastleFrostMantleTest, FrostMantleRingOnAnImmuneEnemyStrikesTheRingButDoesNotChill)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(immune(enemy, BonusType::SPELL_IMMUNITY, 0, BonusSubtypeID(SpellID(SpellID::FROST_RING))));
	EXPECT_TRUE(castableAt(enemy->getPosition())) << "The ring still reaches the adjacent Angels";
	const auto health = actor->getAvailableHealth();
	ASSERT_TRUE(submit(spellAtHex(ATTACKER, SpellID::FROST_RING, enemy->getPosition())));
	EXPECT_LT(actor->getAvailableHealth(), health);
	EXPECT_EQ(chill(*enemy), 0) << "Frost Ring immunity blocks the chill";
	EXPECT_FALSE(cloaked(*enemy));
}

TEST_F(CastleFrostMantleTest, FrostMantleCannotRingAnIsolatedEnemyImmuneToFrostRing)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:adelaide"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FROST_RING, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	active = place(ATTACKER, "core:pikeman", BattleHex(1, 1), 1, 1000);
	actor = place(ATTACKER, "core:crusader", BattleHex(5, 5), 20, 800);
	enemy = place(DEFENDER, "core:halberdier", BattleHex(12, 5), 30, 400);
	const auto * iceElementals = place(DEFENDER, "core:iceElemental", BattleHex(12, 1), 10, 300);
	const auto * levelImmune = place(DEFENDER, "core:halberdier", BattleHex(12, 9), 10, 200);
	const auto * resistant = place(DEFENDER, "core:halberdier", BattleHex(8, 9), 10, 100);
	ASSERT_TRUE(active && actor && enemy && iceElementals && levelImmune && resistant);
	ASSERT_NO_FATAL_FAILURE(immune(levelImmune, BonusType::LEVEL_SPELL_IMMUNITY, 5));
	ASSERT_NO_FATAL_FAILURE(immune(resistant, BonusType::MAGIC_RESISTANCE, 100));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	// Spell and level immunity: the ring reaches nobody and the centre cannot be
	// chilled, so the cast has no effect and stays illegal.
	for(const auto * immuneCentre : {iceElementals, levelImmune})
		EXPECT_FALSE(castableAt(immuneCentre->getPosition())) << immuneCentre->getDescription();
	EXPECT_TRUE(castableAt(enemy->getPosition())) << "An ordinary isolated enemy can still be chilled";
	EXPECT_TRUE(castableAt(resistant->getPosition())) << "Magic resistance is capped at 75%, so it is never immunity";
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(submit(spellAtHex(ATTACKER, SpellID::FROST_RING, iceElementals->getPosition())));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(chill(*iceElementals), 0);
	ASSERT_TRUE(submit(spellAtHex(ATTACKER, SpellID::FROST_RING, enemy->getPosition())));
	EXPECT_EQ(chill(*enemy), -20);
}
