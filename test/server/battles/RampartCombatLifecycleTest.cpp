/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RampartCombatSpecialtyFixture.h"
#include "../../../lib/battle/BattleInfo.h"

using namespace rampartCombatTest;

namespace
{
/// Rampart effect lifecycles through the authoritative processor: recasts refresh,
/// never leaving a second copy.
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
