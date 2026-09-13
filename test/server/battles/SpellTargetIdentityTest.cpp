/*
 * SpellTargetIdentityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"

class SpellTargetIdentityTest : public HeroCommandFixture
{
protected:
	void prepareSpell()
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
		attackerSideHero->addSpellToSpellbook(SpellID::RESURRECTION);
		attackerSideHero->mana = 1000;
	}

	void kill(CStack * unit)
	{
		auto state = unit->acquireState();
		auto damage = unit->getAvailableHealth();
		state->damage(damage);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		change.changedStacks.back().healthDelta = -damage;
		gameHandler->sendAndApply(change);
		ASSERT_FALSE(unit->alive());
		ASSERT_FALSE(unit->isGhost());
	}

	BattleAction resurrect(const CStack * unit)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::RESURRECTION;
		action.aimToUnit(unit);
		return action;
	}
};

TEST_F(SpellTargetIdentityTest, CanonicalCreatureIdentityDoesNotChangeHexOnlyOrAreaTargets)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpell());
	const auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 1);
	const auto * spell = SpellID(SpellID::RESURRECTION).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	const auto explicitTarget = mechanics->canonicalizeTarget({spells::Destination(unit)});
	ASSERT_EQ(explicitTarget.size(), 1u);
	EXPECT_EQ(explicitTarget.front().unitValue, unit);
	EXPECT_EQ(explicitTarget.front().hexValue, unit->getPosition());
	const auto hexOnly = mechanics->canonicalizeTarget({spells::Destination(unit->getPosition())});
	ASSERT_EQ(hexOnly.size(), 1u);
	EXPECT_EQ(hexOnly.front().unitValue, nullptr);

	const auto * areaSpell = SpellID(SpellID::FIREBALL).toSpell();
	spells::BattleCast area(battle(), attackerSideHero, spells::Mode::HERO, areaSpell);
	const auto areaTarget = areaSpell->battleMechanics(&area)->canonicalizeTarget({spells::Destination(unit)});
	ASSERT_GT(areaTarget.size(), 1u);
	for(const auto & destination : areaTarget)
		EXPECT_EQ(destination.unitValue, nullptr);
}

TEST_F(SpellTargetIdentityTest, ResurrectionRevivesTheSelectedSecondCorpseNotTheFirstOnItsHex)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpell());
	auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 1);
	ASSERT_NO_FATAL_FAILURE(kill(first));
	auto * selected = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), first->getPosition(), 1);
	ASSERT_NO_FATAL_FAILURE(kill(selected));
	const auto * spell = SpellID(SpellID::RESURRECTION).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	ASSERT_TRUE(spell->battleMechanics(&cast)->canBeCastAt({spells::Destination(selected)}));
	const auto cost = attackerSideHero->getSpellCost(spell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), resurrect(selected)));
	EXPECT_TRUE(selected->alive());
	EXPECT_FALSE(first->alive());
	EXPECT_EQ(attackerSideHero->mana, 1000 - cost);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
}

TEST_F(SpellTargetIdentityTest, RejectedExplicitGhostNeverFallsBackToAnotherCorpse)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpell());
	auto * corpse = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 1);
	ASSERT_NO_FATAL_FAILURE(kill(corpse));
	auto * selected = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), corpse->getPosition(), 1);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(selected->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	ASSERT_TRUE(selected->isGhost());
	ASSERT_EQ(selected->getPosition(), corpse->getPosition());
	const auto * spell = SpellID(SpellID::RESURRECTION).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt({spells::Destination(corpse)}));
	EXPECT_FALSE(mechanics->canBeCastAt({spells::Destination(selected)}));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), resurrect(selected)));
	EXPECT_FALSE(corpse->alive());
	EXPECT_TRUE(selected->isGhost());
	EXPECT_EQ(attackerSideHero->mana, 1000);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}
