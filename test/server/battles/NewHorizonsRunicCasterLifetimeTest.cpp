/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RunicReflectionFixture.h"

TEST_F(NewHorizonsRunicReflectionCombatTest, RegisteredDeadCreatureCasterPaysItsOriginalChargeAfterItsOwnAreaDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleUnitsChanged remove; remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(casterArmy->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	casterArmy = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(71), 1);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(69), 1000);
	for(const auto & [type, value] : {std::pair{BonusType::SPELLCASTER, 1}, std::pair{BonusType::CASTS, 1}})
		casterArmy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
			BonusSource::CREATURE_ABILITY, value, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::FIREBALL))));
	BattleSetActiveStack active; active.battleID = BattleID(0);
	active.stack = casterArmy->unitId(); active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	ASSERT_EQ(casterArmy->casts.available(), 1);
	bool found = false;
	std::vector<std::byte> expectedRng;
	for(int seed = 0; seed < 4096 && !found; ++seed)
	{
		GameRandomizer probe(*gameState()); probe.setSeed(seed);
		int nativeSample = 99;
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			const auto sample = probe.getDefault().nextInt(0, 99);
			if(unit == dwarf) nativeSample = sample;
		}
		if(nativeSample >= 40) continue;
		std::vector<const battle::Unit *> pool{casterArmy, reserve};
		std::ranges::sort(pool, {}, &battle::Unit::unitId);
		if(*RandomGeneratorUtil::nextItem(pool, probe.getDefault()) != reserve) continue;
		(void)probe.getDefault().nextInt(0, 99);
		expectedRng = wire(probe); gameHandler->randomizer->setSeed(seed); found = true;
	}
	ASSERT_TRUE(found);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto reserveHealth = reserve->getAvailableHealth(), dwarfHealth = dwarf->getAvailableHealth();
	const auto allowance = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(casterArmy, {battle::Destination(dwarf)}, SpellID::FIREBALL)));
	EXPECT_FALSE(casterArmy->alive());
	EXPECT_FALSE(casterArmy->isGhost());
	EXPECT_EQ(battle()->battleGetUnitByID(casterArmy->unitId()), casterArmy);
	EXPECT_EQ(casterArmy->casts.available(), 0);
	EXPECT_EQ(reserve->getAvailableHealth(), reserveHealth - 25);
	EXPECT_EQ(dwarf->getAvailableHealth(), dwarfHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowance);
	ASSERT_EQ(server.castsOf(SpellID::FIREBALL).size(), 1u);
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}
