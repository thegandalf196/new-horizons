/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RampartCombatSpecialtyFixture.h"

using namespace rampartCombatTest;

TEST_F(RampartCombatSpecialtyTest, GiantSlayerRaisesDamagePerRankStepAndScalesWithLevel)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:coronius"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * centaurs = place(ATTACKER, "core:centaur", BattleHex(5, 5), 20, 1000);
	auto * dragon = place(ATTACKER, "core:greenDragon", BattleHex(2, 9), 1, 900);
	auto * angels = place(DEFENDER, "core:angel", BattleHex(6, 5), 5, 400);
	auto * griffins = place(DEFENDER, "core:griffin", BattleHex(12, 1), 5, 300);
	auto * pikemen = place(DEFENDER, "core:pikeman", BattleHex(12, 9), 5, 200);
	ASSERT_TRUE(centaurs && dragon && angels && griffins && pikemen);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	EXPECT_NEAR(multiplier(centaurs, angels), 1.2, 1e-9) << "Core against Champion: two steps of +10%";
	EXPECT_NEAR(multiplier(centaurs, griffins), 1.1, 1e-9);
	EXPECT_NEAR(multiplier(centaurs, pikemen), 1.0, 1e-9);
	EXPECT_NEAR(multiplier(dragon, angels), 1.0, 1e-9) << "Equal rank";
	EXPECT_NEAR(multiplier(angels, centaurs), 1.0, 1e-9) << "Only his own creatures";
	attackerSideHero->level = 11;
	EXPECT_NEAR(multiplier(centaurs, angels), 1.3, 1e-9) << "+0.5% per step per further level";
	attackerSideHero->level = 40;
	EXPECT_NEAR(multiplier(centaurs, angels), 1.4, 1e-9) << "capped at +20% per step";
	// The estimate the authority and the AI share carries the factor.
	const auto boosted = battle()->battleEstimateDamage(BattleAttackInfo(centaurs, angels, 0, false));
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:clancy")));
	const auto plain = battle()->battleEstimateDamage(BattleAttackInfo(centaurs, angels, 0, false));
	EXPECT_NEAR(static_cast<double>(boosted.damage.max) / plain.damage.max, 1.4, 0.05);
}

TEST_F(RampartCombatSpecialtyTest, GiantSlayerHolyWrathCountsTheTargetsStepsAboveCore)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:coronius"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID(SpellID::decode("new-horizons:holyWrath")),
		"new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	active = place(ATTACKER, "core:centaur", BattleHex(1, 5), 1, 1000);
	auto * angels = place(DEFENDER, "core:angel", BattleHex(12, 1), 50, 400);
	auto * griffins = place(DEFENDER, "core:griffin", BattleHex(12, 5), 200, 300);
	auto * pikemen = place(DEFENDER, "core:pikeman", BattleHex(12, 9), 500, 200);
	ASSERT_TRUE(active && angels && griffins && pikemen);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	const SpellID wrath(SpellID::decode("new-horizons:holyWrath"));
	const auto core = spellDamage(wrath, pikemen);
	ASSERT_GT(core, 0);
	EXPECT_NEAR(static_cast<double>(spellDamage(wrath, griffins)) / core, 1.1, 0.01);
	EXPECT_NEAR(static_cast<double>(spellDamage(wrath, angels)) / core, 1.2, 0.01);
	const auto predicted = spellDamage(wrath, angels);
	const auto health = angels->getAvailableHealth();
	ASSERT_TRUE(submit(spellAt(ATTACKER, wrath, angels)));
	EXPECT_EQ(health - angels->getAvailableHealth(), predicted) << "The cast deals what the forecast shows";
}

TEST_F(RampartCombatSpecialtyTest, HistoricalCoroniusHasNoGiantSlayer)
{
	stripped = {"giantSlayer"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:coronius"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * centaurs = place(ATTACKER, "core:centaur", BattleHex(5, 5), 20, 1000);
	auto * angels = place(DEFENDER, "core:angel", BattleHex(6, 5), 5, 400);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	EXPECT_NEAR(multiplier(centaurs, angels), 1.0, 1e-9);
}

TEST_F(RampartCombatSpecialtyTest, LivingRemedyCureGrantsLevelScaledRegeneration)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:uland", 5));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::CURE, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	injure(actor, actor->getAvailableHealth() - 500);
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::CURE, actor)));
	const auto * regeneration = tagged(*actor, newHorizonsRampart::LIVING_REMEDY_STACKING);
	ASSERT_NE(regeneration, nullptr);
	EXPECT_EQ(regeneration->type, BonusType::HP_REGENERATION);
	EXPECT_EQ(regeneration->turnsRemain, 2);
	// 2% at level 1, +0.25% per further level: 3% of 100 Angels' full Health at level 5.
	EXPECT_EQ(regeneration->val, actor->getCount() * static_cast<int>(actor->getMaxHealth()) * 3 / 100);
	// The stack regenerates at the start of its own next activation.
	injure(actor, actor->getAvailableHealth() - 100);
	const auto wounded = actor->getAvailableHealth();
	ASSERT_TRUE(submit(BattleAction::makeDefend(active)));
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	EXPECT_EQ(actor->getAvailableHealth(), wounded + 100) << "Regeneration heals the wounded top creature";
}

TEST_F(RampartCombatSpecialtyTest, HistoricalUlandCureHasNoRegeneration)
{
	stripped = {"livingRemedy"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:uland"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::CURE, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	injure(actor, actor->getAvailableHealth() - 500);
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::CURE, actor)));
	EXPECT_EQ(tagged(*actor, newHorizonsRampart::LIVING_REMEDY_STACKING), nullptr);
}

TEST_F(RampartCombatSpecialtyTest, SorceressFirstSpellWithinHerLevelCostsNoMana)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:gem"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::CURE, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto * cure = SpellID(SpellID::CURE).toSpell();
	ASSERT_EQ(battle()->battleGetSpellLevel(SpellID::CURE), 1);
	EXPECT_EQ(battle()->battleGetSpellCost(cure, attackerSideHero), 0);
	const auto breakdown = battle()->battleGetSpellCostBreakdown(cure, attackerSideHero);
	ASSERT_FALSE(breakdown.stages.empty());
	EXPECT_EQ(breakdown.stages.back().kind, SpellCostStage::Kind::SORCERESS_OF_ENROTH);
	// With no Mana at all the free first spell is still castable through the authority.
	setTestSpellPointTotal(attackerSideHero, 0);
	injure(actor, actor->getAvailableHealth() - 500);
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::CURE, actor)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	EXPECT_GT(battle()->battleGetSpellCost(cure, attackerSideHero), 0) << "Only the first spell is free";
}

TEST_F(RampartCombatSpecialtyTest, SorceressSpellLevelGrowsEveryFiveLevels)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:gem"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::CURE, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	std::map<int, const CSpell *> byLevel;
	for(const SpellID spell : {SpellID(SpellID::CURE), SpellID(SpellID::BLESS), SpellID(SpellID::HASTE),
		SpellID(SpellID::SLOW), SpellID(SpellID::FIREBALL), SpellID(SpellID::BERSERK), SpellID(SpellID::CHAIN_LIGHTNING),
		SpellID(SpellID::RESURRECTION), SpellID(SpellID::ARMAGEDDON), SpellID(SpellID::SACRIFICE)})
		byLevel.emplace(battle()->battleGetSpellLevel(spell), spell.toSpell());
	ASSERT_TRUE(byLevel.contains(2) && byLevel.contains(5));
	attackerSideHero->level = 4;
	EXPECT_GT(battle()->battleGetSpellCost(byLevel.at(2), attackerSideHero), 0);
	attackerSideHero->level = 5;
	EXPECT_EQ(battle()->battleGetSpellCost(byLevel.at(2), attackerSideHero), 0);
	EXPECT_GT(battle()->battleGetSpellCost(byLevel.at(5), attackerSideHero), 0);
	attackerSideHero->level = 20;
	EXPECT_EQ(battle()->battleGetSpellCost(byLevel.at(5), attackerSideHero), 0);
}

TEST_F(RampartCombatSpecialtyTest, HistoricalGemPaysForHerFirstSpell)
{
	stripped = {"sorceressOfEnroth"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:gem"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::CURE, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	EXPECT_GT(battle()->battleGetSpellCost(SpellID(SpellID::CURE).toSpell(), attackerSideHero), 0);
}

TEST_F(RampartCombatSpecialtyTest, BrittleIceBoltEndsDefendAndRaisesPhysicalDamage)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:alagar"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::ICE_BOLT, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(submit(BattleAction::makeDefend(enemy), PlayerColor(1)));
	ASSERT_TRUE(enemy->defended());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::ICE_BOLT, enemy)));
	EXPECT_FALSE(enemy->defended()) << "The ice spell cancels the Defend";
	EXPECT_EQ(newHorizonsRampart::brittlePermille(*enemy), 50);
	EXPECT_NEAR(multiplier(actor, enemy), 1.05, 1e-9);
	// The stack's next activation ends the Brittle mark.
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	EXPECT_EQ(newHorizonsRampart::brittlePermille(*enemy), 0);
}

TEST_F(RampartCombatSpecialtyTest, BrittleScalesWithLevelAndHistoricalAlagarKeepsTheDefend)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:alagar", 21));
	EXPECT_EQ(newHorizonsHeroes::brittlePermille(attackerSideHero), 150);
	attackerSideHero->level = 2;
	EXPECT_EQ(newHorizonsHeroes::brittlePermille(attackerSideHero), 55);
}

TEST_F(RampartCombatSpecialtyTest, HistoricalAlagarIceBoltLeavesTheDefend)
{
	stripped = {"brittle"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:alagar"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::ICE_BOLT, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(submit(BattleAction::makeDefend(enemy), PlayerColor(1)));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(submit(spellAt(ATTACKER, SpellID::ICE_BOLT, enemy)));
	EXPECT_TRUE(enemy->defended());
	EXPECT_EQ(newHorizonsRampart::brittlePermille(*enemy), 0);
}

TEST_F(RampartCombatSpecialtyTest, AgainstTheOddsGrantsLuckOnlyAgainstAStrongerArmy)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:melodia", 10));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * centaurs = place(ATTACKER, "core:centaur", BattleHex(5, 5), 10, 1000);
	ASSERT_TRUE(place(DEFENDER, "core:angel", BattleHex(12, 5), 10, 400));
	const auto heroLuck = attackerSideHero->luckVal();
	const auto stackLuck = centaurs->luckVal();
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto * luck = tagged(*centaurs, newHorizonsRampart::AGAINST_THE_ODDS_STACKING);
	ASSERT_NE(luck, nullptr);
	EXPECT_EQ(luck->type, BonusType::LUCK);
	EXPECT_EQ(luck->val, 2) << "+1, +1 per 10 hero levels";
	EXPECT_EQ(centaurs->luckVal(), std::min(stackLuck + 2, 3)) << "Her stacks gain the Luck";
	EXPECT_EQ(attackerSideHero->luckVal(), heroLuck) << "Her own spell Luck rolls are unchanged";
	EXPECT_FALSE(attackerSideHero->hasBonus(Selector::type()(BonusType::LUCK).And(CSelector([](const Bonus * bonus)
		{ return bonus->stacking == newHorizonsRampart::AGAINST_THE_ODDS_STACKING; }))));
}

TEST_F(RampartCombatSpecialtyTest, AgainstTheOddsStaysOffAgainstAWeakerArmy)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:melodia", 20));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * angels = place(ATTACKER, "core:angel", BattleHex(5, 5), 10, 1000);
	ASSERT_TRUE(place(DEFENDER, "core:pikeman", BattleHex(12, 5), 10, 400));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	EXPECT_EQ(tagged(*angels, newHorizonsRampart::AGAINST_THE_ODDS_STACKING), nullptr);
}

TEST_F(RampartCombatSpecialtyTest, UseTheGroundProtectsStacksBesideAnObstacle)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:mephala"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * guarded = place(ATTACKER, "core:dwarf", BattleHex(5, 5), 20, 1000);
	auto * open = place(ATTACKER, "core:dwarf", BattleHex(5, 9), 20, 900);
	auto * enemyDwarves = place(DEFENDER, "core:dwarf", BattleHex(6, 5), 20, 400);
	ASSERT_TRUE(guarded && open && enemyDwarves);
	ASSERT_NO_FATAL_FAILURE(forceField(BattleHex(4, 5)));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	EXPECT_TRUE(newHorizonsRampart::besideObstacleOrWall(*battle(), *guarded));
	EXPECT_FALSE(newHorizonsRampart::besideObstacleOrWall(*battle(), *open));
	EXPECT_EQ(newHorizonsRampart::useTheGroundBasisPoints(*battle(), BattleAttackInfo(enemyDwarves, guarded, 0, false)), 500);
	EXPECT_EQ(newHorizonsRampart::useTheGroundBasisPoints(*battle(), BattleAttackInfo(enemyDwarves, open, 0, false)), 0);
	attackerSideHero->level = 3;
	EXPECT_EQ(newHorizonsRampart::useTheGroundBasisPoints(*battle(), BattleAttackInfo(enemyDwarves, guarded, 0, false)), 600);
	EXPECT_EQ(newHorizonsRampart::useTheGroundBasisPoints(*battle(), BattleAttackInfo(guarded, enemyDwarves, 0, false)), 0)
		<< "Only her own stacks";
	const auto protectedHit = battle()->battleEstimateDamage(BattleAttackInfo(enemyDwarves, guarded, 0, false));
	const auto openHit = battle()->battleEstimateDamage(BattleAttackInfo(enemyDwarves, open, 0, false));
	EXPECT_NEAR(static_cast<double>(protectedHit.damage.max) / openHit.damage.max, 0.94, 0.02);
}

TEST_F(RampartCombatSpecialtyTest, PerfectMountReducesMagicalDamageUntilImmunityAtLevelTwenty)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:aeris"));
	ASSERT_NO_FATAL_FAILURE(installPassives());
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::ICE_BOLT, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	active = place(ATTACKER, "core:centaur", BattleHex(1, 5), 1, 1000);
	auto * pegasi = place(ATTACKER, "core:pegasus", BattleHex(5, 5), 30, 900);
	auto * centaurs = place(ATTACKER, "core:centaur", BattleHex(5, 9), 30, 800);
	ASSERT_TRUE(active && pegasi && centaurs && place(DEFENDER, "core:pikeman", BattleHex(12, 5), 5, 400));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	EXPECT_FALSE(pegasi->hasBonusOfType(BonusType::MAGICAL_DAMAGE_IMMUNITY));
	EXPECT_EQ(newHorizonsRampart::perfectMountReductionPercent(*pegasi), 43);
	EXPECT_EQ(newHorizonsRampart::perfectMountReductionPercent(*centaurs), 0);
	ASSERT_NO_FATAL_FAILURE(activate(active));
	const auto pegasus = spellDamage(SpellID::ICE_BOLT, pegasi);
	const auto centaur = spellDamage(SpellID::ICE_BOLT, centaurs);
	ASSERT_GT(centaur, 0);
	EXPECT_NEAR(static_cast<double>(pegasus) / centaur, 0.57, 0.02) << "43% less magical damage at level 1";
	attackerSideHero->level = 20;
	newHorizonsHeroes::refreshLevelScaledSpecialtyBonus(*attackerSideHero);
	EXPECT_TRUE(pegasi->hasBonusOfType(BonusType::MAGICAL_DAMAGE_IMMUNITY)) << "Immune from level 20";
	EXPECT_EQ(newHorizonsRampart::perfectMountReductionPercent(*pegasi), 0) << "The reduction is replaced";
	EXPECT_EQ(spellDamage(SpellID::ICE_BOLT, pegasi), 0);
}

TEST_F(RampartCombatSpecialtyTest, TreantmootDendroidsTakeLessPhysicalDamage)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:ryland", 11));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * dendroids = place(ATTACKER, "core:dendroidGuard", BattleHex(5, 5), 10, 1000);
	auto * enemyDwarves = place(DEFENDER, "core:dwarf", BattleHex(6, 5), 20, 400);
	ASSERT_TRUE(dendroids && enemyDwarves);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto before = battle()->battleEstimateDamage(BattleAttackInfo(enemyDwarves, dendroids, 0, false));
	ASSERT_NO_FATAL_FAILURE(installPassives());
	const auto after = battle()->battleEstimateDamage(BattleAttackInfo(enemyDwarves, dendroids, 0, false));
	EXPECT_EQ(dendroids->valOfBonuses(BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS), 2000) << "10% +1% per further level";
	EXPECT_NEAR(static_cast<double>(after.damage.max) / before.damage.max, 0.8, 0.02);
}

TEST_F(RampartCombatSpecialtyTest, ForestAmbushShotFromHidingGainsTwoPercentPerLevel)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:ivor", 15));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * elves = place(ATTACKER, "core:woodElf", BattleHex(2, 5), 20, 1000);
	auto * centaurs = place(ATTACKER, "core:centaur", BattleHex(2, 9), 20, 900);
	auto * enemyDwarves = place(DEFENDER, "core:dwarf", BattleHex(12, 5), 20, 400);
	ASSERT_TRUE(elves && centaurs && enemyDwarves);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(elves));
	EXPECT_NEAR(multiplier(elves, enemyDwarves, true), 1.0, 1e-9) << "Not hidden yet";
	ASSERT_TRUE(submit(BattleAction::makeWait(elves)));
	ASSERT_NO_FATAL_FAILURE(activate(elves));
	EXPECT_NEAR(multiplier(elves, enemyDwarves, true), 1.3, 1e-9) << "Delayed shot after hiding: +2% per hero level";
	EXPECT_NEAR(multiplier(elves, enemyDwarves, false), 1.0, 1e-9) << "Shots only";
}
