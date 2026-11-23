/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RampartCombatSpecialtyFixture.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/NewHorizonsArmorer.h"

using namespace rampartCombatTest;

namespace
{
/// Rampart effect lifecycles through the authoritative processor: recasts refresh,
/// delayed shots apply their bonus once, and Brittle meets Armorer's Last Stand.
class RampartCombatLifecycleTest : public RampartCombatSpecialtyTest
{
protected:
	size_t copies(const battle::Unit & unit, const char * stacking) const
	{
		return unit.getBonuses(CSelector([stacking](const Bonus * bonus) { return bonus->stacking == stacking; }))->size();
	}

	/// Makes the side's hero able to cast again this round, as if a new round began.
	void restoreLedger(const SideInBattle & ledger)
	{
		battle()->getSide(ATTACKER) = ledger;
	}

	/// The physical damage of each primary (non-retaliation) strike `attacker` made since `from`.
	std::vector<int64_t> strikes(const CStack * attacker, const CStack * target, size_t from) const
	{
		std::vector<int64_t> result;
		for(size_t index = from; index < server.attacks.size(); ++index)
		{
			const auto & attack = server.attacks[index];
			if(attack.stackAttacking != attacker->unitId() || attack.counter())
				continue;
			for(const auto & hit : attack.bsa)
				if(hit.stackAttacked == target->unitId())
					result.push_back(hit.damageAmount);
		}
		return result;
	}
};
}

TEST_F(RampartCombatLifecycleTest, ARecastCureRefreshesLivingRemedyInsteadOfAddingACopy)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:uland", 5));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::CURE, "new-horizons:lightMagic"));
	setTestSpellPointTotal(attackerSideHero, 200);
	ASSERT_NO_FATAL_FAILURE(arena());
	injure(actor, actor->getAvailableHealth() - 500);
	const auto ledger = battle()->getSide(ATTACKER);
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::CURE, actor)));
	ASSERT_EQ(copies(*actor, newHorizonsRampart::LIVING_REMEDY_STACKING), 1);
	// Age the first copy so the refresh is visible in its duration too.
	const_cast<Bonus *>(tagged(*actor, newHorizonsRampart::LIVING_REMEDY_STACKING))->turnsRemain = 1;
	restoreLedger(ledger);
	attackerSideHero->level = 9;
	injure(actor, actor->getAvailableHealth() - 500); // Cure needs a wound to accept the target.
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::CURE, actor)));
	EXPECT_EQ(copies(*actor, newHorizonsRampart::LIVING_REMEDY_STACKING), 1) << "A new Cure refreshes it";
	const auto * regeneration = tagged(*actor, newHorizonsRampart::LIVING_REMEDY_STACKING);
	ASSERT_NE(regeneration, nullptr);
	EXPECT_EQ(regeneration->turnsRemain, 2) << "The newer duration wins";
	EXPECT_EQ(regeneration->val, actor->getCount() * static_cast<int>(actor->getMaxHealth()) * 4 / 100)
		<< "The newer value (4% at level 9) wins";
}

TEST_F(RampartCombatLifecycleTest, ARecastIceBoltRefreshesBrittleInsteadOfAddingACopy)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:alagar"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::ICE_BOLT, "new-horizons:havocMagic"));
	setTestSpellPointTotal(attackerSideHero, 200);
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	const auto ledger = battle()->getSide(ATTACKER);
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::ICE_BOLT, enemy)));
	ASSERT_EQ(newHorizonsRampart::brittlePermille(*enemy), 50);
	restoreLedger(ledger);
	attackerSideHero->level = 2;
	ASSERT_TRUE(enemy->alive());
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::ICE_BOLT, enemy)));
	EXPECT_EQ(copies(*enemy, newHorizonsRampart::BRITTLE_STACKING), 1) << "A new ice spell refreshes the mark";
	EXPECT_EQ(newHorizonsRampart::brittlePermille(*enemy), 55) << "The newer value wins";
}

TEST_F(RampartCombatLifecycleTest, IvorsDelayedGrandElfShotAppliesTheBonusToItsFirstShotOnly)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:ivor", 15));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * elves = place(ATTACKER, "core:grandElf", BattleHex(2, 5), 20, 1000);
	auto * enemyDwarves = place(DEFENDER, "core:dwarf", BattleHex(12, 5), 300, 400);
	ASSERT_TRUE(elves && enemyDwarves);
	forceMaximumDamage(elves);
	effect(elves, Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(elves));
	ASSERT_TRUE(submit(BattleAction::makeWait(elves)));
	ASSERT_NO_FATAL_FAILURE(activate(elves));
	ASSERT_NEAR(multiplier(elves, enemyDwarves, true), 1.3, 1e-9);
	const auto from = server.attacks.size();
	ASSERT_TRUE(submit(BattleAction::makeShotAttack(elves, enemyDwarves)));
	const auto shots = strikes(elves, enemyDwarves, from);
	ASSERT_EQ(shots.size(), 2) << "Grand Elves shoot twice";
	ASSERT_GT(shots[1], 0);
	EXPECT_NEAR(static_cast<double>(shots[0]) / shots[1], 1.3, 0.03)
		<< "The applied first shot carries +30%; the second shot is no longer from hiding";
	EXPECT_NEAR(multiplier(elves, enemyDwarves, true), 1.0, 1e-9) << "The hidden shot is spent";
}

TEST_F(RampartCombatLifecycleTest, BrittleRefusesLastStandsDefendButKeepsItsSurvival)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:alagar"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::ICE_BOLT, "new-horizons:havocMagic"));
	const std::string armorer = newHorizonsArmorer::SKILL_ID;
	ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, armorer, "new-horizons:armorer.pavise", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, armorer, "new-horizons:armorer.veteran", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, armorer, newHorizonsArmorer::LAST_STAND_PERK_ID, MasteryLevel::EXPERT));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	active = place(ATTACKER, "core:pikeman", BattleHex(1, 5), 1, 1000);
	auto * angels = place(ATTACKER, "core:angel", BattleHex(7, 5), 30, 800);
	auto * target = place(DEFENDER, "core:pikeman", BattleHex(8, 5), 20, 400);
	ASSERT_TRUE(active && angels && target);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::ICE_BOLT, target)));
	ASSERT_TRUE(target->alive());
	ASSERT_GT(newHorizonsRampart::brittlePermille(*target), 0);
	ASSERT_NO_FATAL_FAILURE(activate(angels));
	ASSERT_TRUE(submit(BattleAction::makeMeleeAttack(angels, target, angels->getPosition())));
	EXPECT_TRUE(target->alive()) << "Last Stand still saves the stack";
	EXPECT_EQ(target->getCount(), 1);
	EXPECT_EQ(target->getAvailableHealth(), 1);
	EXPECT_TRUE(battle()->armorerLastStandUsed(DEFENDER));
	EXPECT_FALSE(target->defended()) << "Brittle refuses the Defend stance Last Stand would add";
	EXPECT_FALSE(target->hasBonusOfType(BonusType::UNIT_DEFENDING));
}
