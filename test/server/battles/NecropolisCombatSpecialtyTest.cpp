/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/entities/hero/NewHorizonsNecropolisCombatSpecialties.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../SpellPointTestUtils.h"
namespace
{
class NecropolisCombatSpecialtyTest : public CastleSpecialtyTest
{
protected:
	void injure(const CStack * unit, int64_t amount)
	{
		auto next = unit->acquireState();
		next->damage(amount);
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		UnitChanges update(unit->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = next->save();
		update.healthDelta = -amount;
		packet.changedStacks.push_back(update);
		gameHandler->sendAndApply(packet);
	}
};
}
TEST_F(NecropolisCombatSpecialtyTest, ResidualLifeReceivesActualSpellHpDamageOnce)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:septienna"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
	setTestSpellPointTotal(attackerSideHero, 200);
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	const auto health = enemy->getAvailableHealth();
	const auto buffer = attackerSideHero->getBufferSpellPoints();
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, enemy)));
	const auto damage = health - enemy->getAvailableHealth();
	const auto threshold = newHorizonsHeroes::residualLifeThreshold(attackerSideHero);
	ASSERT_GT(damage, 0);
	EXPECT_EQ(battle()->getResidualLifeDamage(BattleSide::ATTACKER), std::min<int64_t>(damage, 20 * threshold));
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints() - buffer, std::min<int64_t>(20, damage / threshold));
}
TEST_F(NecropolisCombatSpecialtyTest, HistoricalSeptiennaReceivesNoBuffer)
{
	stripped = {"residualLife"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:septienna"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
	setTestSpellPointTotal(attackerSideHero, 200);
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	const auto buffer = attackerSideHero->getBufferSpellPoints();
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, enemy)));
	EXPECT_EQ(battle()->getResidualLifeDamage(BattleSide::ATTACKER), 0);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), buffer);
}
TEST_F(NecropolisCombatSpecialtyTest, AislinnLifeDrainIncreasesActualDamageAndHealing)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:aislinn", 10));
	const SpellID drain(SpellID::decode("new-horizons:lifeDrain"));
	ASSERT_TRUE(drain.hasValue());
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, drain, "new-horizons:shadowMagic"));
	setTestSpellPointTotal(attackerSideHero, 200);
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * friendly = place(BattleSide::ATTACKER, "core:angel", BattleHex(3, 5), 20, 1000);
	auto * victim = place(BattleSide::DEFENDER, "core:pikeman", BattleHex(12, 5), 500, 100);
	ASSERT_TRUE(friendly && victim);
	injure(friendly, 150);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(friendly));
	spells::BattleCast probe(battle(), attackerSideHero, spells::Mode::HERO, drain.toSpell());
	const auto mechanics = drain.toSpell()->battleMechanics(&probe);
	const auto ordinary = mechanics->adjustEffectValue(victim);
	const auto expected = ordinary + ordinary * 50 / 100;
	const auto enemyBefore = victim->getAvailableHealth();
	const auto friendBefore = friendly->getAvailableHealth();
	auto action = spellAt(BattleSide::ATTACKER, drain, victim);
	action.aimToUnit(friendly);
	ASSERT_TRUE(submit(action));
	EXPECT_EQ(enemyBefore - victim->getAvailableHealth(), expected);
	EXPECT_GT(friendly->getAvailableHealth(), friendBefore);
}
TEST_F(NecropolisCombatSpecialtyTest, XsiAddsAnIndependentFractionalPhysicalReduction)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:xsi", 2));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * friendly = place(BattleSide::ATTACKER, "core:pikeman", BattleHex(7, 5), 200, 300);
	auto * hostile = place(BattleSide::DEFENDER, "core:angel", BattleHex(8, 5), 10, 1000);
	ASSERT_TRUE(friendly && hostile);
	const auto reduced = battle()->battleEstimateDamage(BattleAttackInfo(hostile, friendly, 0, false));
	EXPECT_EQ(newHorizonsHeroes::unyieldingBonesPermille(attackerSideHero), 55);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:ivor")));
	const auto ordinary = battle()->battleEstimateDamage(BattleAttackInfo(hostile, friendly, 0, false));
	EXPECT_NEAR(static_cast<double>(reduced.damage.max) / ordinary.damage.max, 0.945, 0.01);
}

TEST_F(NecropolisCombatSpecialtyTest, ResidualRestoreRejectsWrongHeroAndExcessProgress)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:septienna"));
	ASSERT_NO_FATAL_FAILURE(arena());
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.residualLifeDamage = 49;
	EXPECT_NO_THROW(side.validateResidualLifeSerialization(true, attackerSideHero));
	EXPECT_THROW(side.validateResidualLifeSerialization(false, attackerSideHero), std::runtime_error);
	side.residualLifeDamage = 981;
	EXPECT_THROW(side.validateResidualLifeSerialization(true, attackerSideHero), std::runtime_error);
	side.residualLifeDamage = 49;
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:ivor")));
	EXPECT_THROW(side.validateResidualLifeSerialization(true, attackerSideHero), std::runtime_error);
}
