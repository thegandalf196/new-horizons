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
	void injure(const CStack * unit, int64_t amount, bool destroyRemains = false)
	{
		auto next = unit->acquireState();
		next->damage(amount, destroyRemains);
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
	const auto total = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell(), attackerSideHero);
	ASSERT_GT(cost, 0);
	const auto paidBuffer = std::min<int64_t>(buffer, cost);
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, enemy)));
	const auto damage = health - enemy->getAvailableHealth();
	const auto threshold = newHorizonsHeroes::residualLifeThreshold(attackerSideHero);
	ASSERT_GT(damage, 0);
	EXPECT_EQ(battle()->getResidualLifeDamage(BattleSide::ATTACKER), std::min<int64_t>(damage, 20 * threshold));
	const auto granted = std::min<int64_t>(20, damage / threshold);
	// The scalar fixture total can be Buffer when Normal capacity is zero.
	// Spell payment spends that existing Buffer before Residual Life adds its grant.
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), buffer - paidBuffer + granted);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), total - cost + granted);
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
	const auto total = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(SpellID(SpellID::MAGIC_ARROW).toSpell(), attackerSideHero);
	ASSERT_GT(cost, 0);
	const auto paidBuffer = std::min<int64_t>(buffer, cost);
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, enemy)));
	EXPECT_EQ(battle()->getResidualLifeDamage(BattleSide::ATTACKER), 0);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), buffer - paidBuffer);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), total - cost);
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

TEST_F(NecropolisCombatSpecialtyTest, ReanimateHumanHexHealsLivingCasualtiesLikeExplicitUnitTarget)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:thant"));
	const SpellID spell(SpellID::decode("new-horizons:reanimate"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, spell, "new-horizons:shadowMagic"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * target = place(BattleSide::ATTACKER, "core:skeleton", BattleHex(3, 5), 100, 1000);
	ASSERT_NE(place(BattleSide::DEFENDER, "core:pikeman", BattleHex(12, 5), 100, 100), nullptr);
	ASSERT_NE(target, nullptr);
	injure(target, target->getMaxHealth() * 10);
	ASSERT_EQ(target->getCount(), 90);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(target));
	spells::BattleCast probe(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&probe);
	spells::Target hexAim; hexAim.emplace_back(target->getPosition());
	spells::Target unitAim; unitAim.emplace_back(target);
	ASSERT_TRUE(mechanics->canBeCastAt(hexAim)) << "same hex-only aim as the human hover";
	ASSERT_TRUE(mechanics->canBeCastAt(unitAim));
	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(spell.toSpell(), attackerSideHero);
	ASSERT_TRUE(submit(spellAtHex(BattleSide::ATTACKER, spell, target->getPosition())));
	EXPECT_EQ(target->getCount(), 100);
	EXPECT_EQ(target->getAvailableHealth() - before, target->getMaxHealth() * 10);
	EXPECT_EQ(target->getHealth().getResurrected(), 10);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
}

TEST_F(NecropolisCombatSpecialtyTest, ReanimateHumanHexFindsUnoccupiedDoubleWideCorpseByEitherHex)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:thant"));
	const SpellID spell(SpellID::decode("new-horizons:reanimate"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, spell, "new-horizons:shadowMagic"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * actor = place(BattleSide::ATTACKER, "core:pikeman", BattleHex(3, 2), 100, 1000);
	auto * corpse = place(BattleSide::ATTACKER, "core:hydra", BattleHex(7, 5), 100, 200);
	ASSERT_NE(place(BattleSide::DEFENDER, "core:pikeman", BattleHex(12, 5), 100, 100), nullptr);
	ASSERT_TRUE(actor && corpse && corpse->doubleWide());
	injure(corpse, corpse->getAvailableHealth());
	ASSERT_TRUE(corpse->isDead());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(actor));
	spells::BattleCast probe(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&probe);
	for(const auto hex : corpse->getHexes())
	{
		spells::Target aim; aim.emplace_back(hex);
		EXPECT_TRUE(mechanics->canBeCastAt(aim));
	}
	spells::Target explicitAim; explicitAim.emplace_back(corpse);
	EXPECT_TRUE(mechanics->canBeCastAt(explicitAim));
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(spell.toSpell(), attackerSideHero);
	ASSERT_TRUE(submit(spellAtHex(BattleSide::ATTACKER, spell, corpse->occupiedHex())));
	EXPECT_TRUE(corpse->alive());
	EXPECT_GT(corpse->getHealth().getResurrected(), 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
}

TEST_F(NecropolisCombatSpecialtyTest, ReanimateHumanHexRejectsOccupiedCorpseWithoutRedirectingExplicitUnit)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:thant"));
	const SpellID spell(SpellID::decode("new-horizons:reanimate"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, spell, "new-horizons:shadowMagic"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * actor = place(BattleSide::ATTACKER, "core:pikeman", BattleHex(3, 2), 100, 1000);
	auto * corpse = place(BattleSide::ATTACKER, "core:hydra", BattleHex(7, 5), 100, 200);
	ASSERT_NE(place(BattleSide::DEFENDER, "core:pikeman", BattleHex(12, 5), 100, 100), nullptr);
	ASSERT_TRUE(actor && corpse && corpse->doubleWide());
	injure(corpse, corpse->getAvailableHealth());
	auto * blocker = place(BattleSide::ATTACKER, "core:skeleton", corpse->occupiedHex(), 100, 300);
	ASSERT_NE(blocker, nullptr);
	injure(blocker, blocker->getMaxHealth() * 10);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(actor));
	spells::BattleCast probe(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&probe);
	spells::Target corpseAim; corpseAim.emplace_back(corpse);
	spells::Target blockedFront; blockedFront.emplace_back(corpse->getPosition());
	spells::Target livingAim; livingAim.emplace_back(blocker->getPosition());
	EXPECT_FALSE(mechanics->canBeCastAt(corpseAim));
	EXPECT_FALSE(mechanics->canBeCastAt(blockedFront)) << "the corpse's other hex is occupied";
	EXPECT_TRUE(mechanics->canBeCastAt(livingAim)) << "the living wounded occupant is independently legal";
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(submit(spellAt(BattleSide::ATTACKER, spell, corpse)));
	EXPECT_FALSE(submit(spellAtHex(BattleSide::ATTACKER, spell, corpse->getPosition())));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(blocker->getCount(), 90);
	EXPECT_TRUE(corpse->isDead());
}

TEST_F(NecropolisCombatSpecialtyTest, ReanimateHumanHexRetainsOwnershipWoundsAndUsableRemainsChecks)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:thant"));
	const SpellID spell(SpellID::decode("new-horizons:reanimate"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, spell, "new-horizons:shadowMagic"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * actor = place(BattleSide::ATTACKER, "core:pikeman", BattleHex(3, 2), 100, 1000);
	auto * disintegrated = place(BattleSide::ATTACKER, "core:skeleton", BattleHex(5, 5), 100, 200);
	auto * enemy = place(BattleSide::DEFENDER, "core:pikeman", BattleHex(12, 5), 100, 100);
	ASSERT_TRUE(actor && disintegrated && enemy);
	injure(disintegrated, disintegrated->getMaxHealth() * 10, true);
	injure(enemy, enemy->getMaxHealth() * 10);
	ASSERT_EQ(disintegrated->getUnusableRemains(), 10);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(actor));
	spells::BattleCast probe(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&probe);
	const auto mana = attackerSideHero->getManaAvailable();
	for(const auto hex : {actor->getPosition(), disintegrated->getPosition(), enemy->getPosition(), BattleHex(9, 2)})
	{
		spells::Target aim; aim.emplace_back(hex);
		EXPECT_FALSE(mechanics->canBeCastAt(aim));
		EXPECT_FALSE(submit(spellAtHex(BattleSide::ATTACKER, spell, hex)));
	}
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(disintegrated->getCount(), 90);
}
