/*
 * NewHorizonsCombatSkillsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"

namespace
{
class NewHorizonsCombatSkillsTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	SecondarySkill skill(const char * identifier) const
	{
		const int decoded = SecondarySkill::decode(identifier);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void setPavise(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(skill("new-horizons:armorer"), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.pavise"});
		ASSERT_TRUE(hero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.pavise"));
	}
};
}

TEST(NewHorizonsCombatSkillsRulesTest, RankTablesMatchCanonicalPercentages)
{
	EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(0), 0);
	EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(1), 5);
	EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(2), 10);
	EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(3), 15);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(0), 0);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(1), 10);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(2), 20);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(3), 30);
}

TEST(NewHorizonsCombatSkillsRulesTest, CounterchargeAddsTwentyFivePointsAndCapsAtOneHundred)
{
	EXPECT_EQ(newHorizonsCombatSkills::bracePreemptivePercent(50, false), 50);
	EXPECT_EQ(newHorizonsCombatSkills::bracePreemptivePercent(50, true), 75);
	EXPECT_EQ(newHorizonsCombatSkills::bracePreemptivePercent(75, true), 100);
	EXPECT_EQ(newHorizonsCombatSkills::bracePreemptivePercent(100, true), 100);
	EXPECT_EQ(newHorizonsCombatSkills::bracePreemptivePercent(110, false), 110)
		<< "Without Countercharge preserve the authored Brace formula unchanged";
	EXPECT_EQ(newHorizonsCombatSkills::bracePreemptivePercent(0, true), 0)
		<< "Countercharge does not create a strike when Brace's authored coefficient is zero";
}

TEST_F(NewHorizonsCombatSkillsTest, ExpertArmorerReducesOnlyPhysicalCreatureDamage)
{
	startGame();
	defenderSideHero->setSecSkillLevel(skill("new-horizons:armorer"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	forceMaximumDamage(attacker);
	BattleAttackInfo physical(attacker, defender, 0, false);
	BattleAttackInfo nonPhysical(attacker, defender, 0, false);
	nonPhysical.physicalDamage = false;
	const auto physicalDamage = battle()->calculateDmgRange(physical).damage.max;
	const auto nonPhysicalDamage = battle()->calculateDmgRange(nonPhysical).damage.max;
	EXPECT_EQ(physicalDamage, nonPhysicalDamage * 85 / 100);
}

TEST_F(NewHorizonsCombatSkillsTest, ExpertArcheryAddsThirtyPercentOnlyToPhysicalShots)
{
	startGame();
	attackerSideHero->setSecSkillLevel(skill("new-horizons:archery"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	forceMaximumDamage(attacker);
	BattleAttackInfo physical(attacker, defender, 0, true);
	BattleAttackInfo nonPhysical(attacker, defender, 0, true);
	nonPhysical.physicalDamage = false;
	const auto physicalDamage = battle()->calculateDmgRange(physical).damage.max;
	const auto nonPhysicalDamage = battle()->calculateDmgRange(nonPhysical).damage.max;
	EXPECT_EQ(nonPhysicalDamage, 7200);
	EXPECT_EQ(physicalDamage, 9000);
}

TEST_F(NewHorizonsCombatSkillsTest, SpellLikeCreatureShotIsClassifiedNonPhysicalForServerAndAI)
{
	startGame();
	startBattle();
	auto * lich = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 10);
	ASSERT_TRUE(lich->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	const BattleAttackInfo shot(lich, target, 0, true);
	EXPECT_FALSE(shot.physicalDamage);
	const auto retaliation = shot.reverse();
	EXPECT_TRUE(retaliation.physicalDamage);
}

TEST_F(NewHorizonsCombatSkillsTest, LichPhysicalMeleeAndRetaliationStillReceiveArmorer)
{
	startGame();
	attackerSideHero->setSecSkillLevel(skill("new-horizons:armorer"), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	defenderSideHero->setSecSkillLevel(skill("new-horizons:armorer"), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * lich = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(leftHex), 10);
	auto * angel = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	ASSERT_TRUE(lich->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	EXPECT_TRUE(newHorizonsCombatSkills::isOrdinaryCreatureAttacker(lich));
	forceMaximumDamage(lich);
	forceMaximumDamage(angel);

	const BattleAttackInfo lichMelee(lich, angel, 0, false);
	BattleAttackInfo nonPhysicalLichMelee(lich, angel, 0, false);
	nonPhysicalLichMelee.physicalDamage = false;
	EXPECT_TRUE(lichMelee.physicalDamage);
	EXPECT_EQ(battle()->calculateDmgRange(lichMelee).damage.max,
		battle()->calculateDmgRange(nonPhysicalLichMelee).damage.max * 85 / 100)
		<< "Spell-like ranged identity must not suppress Armorer from physical melee";

	const BattleAttackInfo incomingMelee(angel, lich, 0, false);
	const auto lichRetaliation = incomingMelee.reverse();
	ASSERT_EQ(lichRetaliation.attacker, lich);
	ASSERT_TRUE(lichRetaliation.physicalDamage);
	BattleAttackInfo nonPhysicalRetaliation = lichRetaliation;
	nonPhysicalRetaliation.physicalDamage = false;
	EXPECT_EQ(battle()->calculateDmgRange(lichRetaliation).damage.max,
		battle()->calculateDmgRange(nonPhysicalRetaliation).damage.max * 85 / 100)
		<< "A Lich's spell-like ranged attack identity must not suppress Armorer from its physical retaliation";
}

TEST_F(NewHorizonsCombatSkillsTest, PaviseIsIndependentAndOnlyProtectsDefendingTargetsFromPhysicalShots)
{
	startGame();
	setPavise(defenderSideHero);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 6), 100);
	auto * lich = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(leftHex + 4), 10);
	forceMaximumDamage(attacker);
	const BattleAttackInfo shot(attacker, defender, 0, true);
	const BattleAttackInfo melee(attacker, defender, 0, false);
	BattleAttackInfo magicalShot(attacker, defender, 0, true);
	magicalShot.physicalDamage = false;
	BattleAttackInfo collateralShot(attacker, collateral, 0, true);
	collateralShot.secondaryAttack = true;
	const auto baseline = battle()->calculateDmgRange(shot).damage.max;
	const auto magicalBaseline = battle()->calculateDmgRange(magicalShot).damage.max;
	const auto collateralBaseline = battle()->calculateDmgRange(collateralShot).damage.max;
	ASSERT_GT(baseline, 0);
	ASSERT_GT(magicalBaseline, 0);
	ASSERT_GT(collateralBaseline, 0);
	EXPECT_EQ(battle()->calculateDmgRange(melee).damage.max, baseline);
	EXPECT_EQ(battle()->calculateDmgRange(magicalShot).damage.max, magicalBaseline);
	EXPECT_EQ(battle()->calculateDmgRange(shot).damage.max, baseline)
		<< "Pavise is inactive before this stack Defends";

	defender->defending = true;
	collateral->defending = true;
	EXPECT_EQ(battle()->calculateDmgRange(shot).damage.max, baseline * 75 / 100);
	EXPECT_EQ(battle()->calculateDmgRange(collateralShot).damage.max, collateralBaseline * 75 / 100)
		<< "Each separately evaluated qualifying collateral target receives Pavise reduction";
	EXPECT_EQ(battle()->calculateDmgRange(melee).damage.max, baseline)
		<< "Pavise never reduces melee physical damage";
	EXPECT_EQ(battle()->calculateDmgRange(magicalShot).damage.max, magicalBaseline)
		<< "Pavise never reduces magical damage";
	const BattleAttackInfo spellLikeShot(lich, defender, 0, true);
	ASSERT_TRUE(lich->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	EXPECT_FALSE(spellLikeShot.physicalDamage);
	EXPECT_TRUE(newHorizonsCombatSkills::isOrdinaryCreatureAttacker(lich));
	EXPECT_EQ(battle()->calculateDmgRange(spellLikeShot).damage.max,
		battle()->calculateDmgRange(BattleAttackInfo(lich, defender, 0, true)).damage.max);
}

TEST_F(NewHorizonsCombatSkillsTest, DefendingStackWithoutThePerkGetsNoPaviseReduction)
{
	startGame();
	defenderSideHero->setSecSkillLevel(skill("new-horizons:armorer"), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	forceMaximumDamage(attacker);
	const BattleAttackInfo shot(attacker, defender, 0, true);
	const auto before = battle()->calculateDmgRange(shot).damage.max;
	ASSERT_GT(before, 0);
	defender->defending = true;
	EXPECT_EQ(battle()->calculateDmgRange(shot).damage.max, before);
	EXPECT_EQ(newHorizonsCombatSkills::paviseReductionPercent(defenderSideHero), 0);
}

TEST_F(NewHorizonsCombatSkillsTest, PaviseIsSuppressedForNonCreatureAttackersAndRespectsTheGlobalCap)
{
	startGame();
	setPavise(defenderSideHero);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	forceMaximumDamage(attacker);
	const BattleAttackInfo shot(attacker, defender, 0, true);
	const auto baseline = battle()->calculateDmgRange(shot).damage.max;
	ASSERT_GT(baseline, 0);
	defender->defending = true;
	const auto pavised = battle()->calculateDmgRange(shot).damage.max;
	EXPECT_EQ(pavised, baseline * 75 / 100);
	auto siegeMarker = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SIEGE_WEAPON,
		BonusSource::OTHER, 1, BonusSourceID());
	attacker->addNewBonus(siegeMarker);
	const auto siegeDamage = battle()->calculateDmgRange(shot).damage.max;
	attacker->removeBonus(siegeMarker);
	EXPECT_GT(siegeDamage, pavised) << "A siege-weapon creature does not receive ordinary-creature PDR handling";

	auto * siege = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(leftHex - 4), 1);
	auto * turret = addStack(BattleSide::ATTACKER, CreatureID::ARROW_TOWERS, BattleHex(leftHex + 34), 1);
	ASSERT_TRUE(siege->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_TRUE(turret->isTurret());
	EXPECT_FALSE(newHorizonsCombatSkills::isOrdinaryCreatureAttacker(siege));
	EXPECT_FALSE(newHorizonsCombatSkills::isOrdinaryCreatureAttacker(turret));
	defender->defending = false;
	const auto siegeBeforeDefend = battle()->calculateDmgRange(BattleAttackInfo(siege, defender, 0, true)).damage.max;
	const auto turretBeforeDefend = battle()->calculateDmgRange(BattleAttackInfo(turret, defender, 0, true)).damage.max;
	defender->defending = true;
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(siege, defender, 0, true)).damage.max, siegeBeforeDefend);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(turret, defender, 0, true)).damage.max, turretBeforeDefend);

	auto reduction = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::SPELL_EFFECT, 80, BonusSourceID(SpellID::AIR_SHIELD),
		BonusSubtypeID(BonusCustomSubtype::damageTypeRanged));
	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	defender->addNewBonus(control);
	const auto unmitigated = battle()->calculateDmgRange(shot).damage.max;
	defender->removeBonus(control);
	ASSERT_GT(unmitigated, 0);
	defender->addNewBonus(reduction);
	const auto capped = battle()->calculateDmgRange(shot).damage.max;
	EXPECT_GE(capped, unmitigated * 20 / 100 - 1)
		<< "An exact 80% ranged reduction sets the lower damage bound even when Armorer and Pavise also apply";
	EXPECT_LE(capped, unmitigated * 20 / 100 + 1)
		<< "The same reduction reaches the 80% cap; integer damage rounding may differ by one point";
}

TEST_F(NewHorizonsCombatSkillsTest, PaviseUsesTheCurrentlyControllingHeroAndDefendLifecycle)
{
	startGame();
	setPavise(defenderSideHero);
	attackerSideHero->setSecSkillLevel(skill("new-horizons:armorer"), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	forceMaximumDamage(attacker);
	const BattleAttackInfo shot(attacker, defender, 0, true);
	const auto baseline = battle()->calculateDmgRange(shot).damage.max;
	ASSERT_GT(baseline, 0);
	defender->defending = true;
	EXPECT_EQ(battle()->calculateDmgRange(shot).damage.max, baseline * 75 / 100);

	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	defender->addNewBonus(control);
	EXPECT_EQ(battle()->battleGetOwnerHero(defender), attackerSideHero);
	const auto * controlledHero = battle()->battleGetOwnerHero(defender);
	EXPECT_EQ(newHorizonsCombatSkills::paviseReductionPercent(controlledHero), 0)
		<< "After control changes, Pavise follows the current controller rather than the original side";
	EXPECT_GT(battle()->calculateDmgRange(shot).damage.max, baseline * 75 / 100);
	defender->removeBonus(control);

	defender->defending = false;
	battle()->activeStack = defender->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), defender->unitOwner(),
		BattleAction::makeDefend(defender)));
	EXPECT_TRUE(defender->defended());
	const auto paviseDamageAfterDefend = battle()->calculateDmgRange(shot).damage.max;
	defender->addNewBonus(control);
	const auto equivalentDefendedDamageWithoutPavise = battle()->calculateDmgRange(shot).damage.max;
	defender->removeBonus(control);
	EXPECT_EQ(paviseDamageAfterDefend, equivalentDefendedDamageWithoutPavise * 75 / 100)
		<< "Compare against an equivalent Defending stack: Defend also grants +20% Defense";
	EXPECT_FALSE(server.battleLogLines.empty());
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Pavise") != std::string::npos;
	}));
	const auto estimatedDamage = paviseDamageAfterDefend;
	battle()->activeStack = attacker->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attacker->unitOwner(),
		BattleAction::makeShotAttack(attacker, defender)));
	ASSERT_FALSE(server.attacks.empty());
	ASSERT_FALSE(server.attacks.back().bsa.empty());
	EXPECT_EQ(server.attacks.back().bsa.front().damageAmount, estimatedDamage)
		<< "Authoritative shot execution uses the same Pavise-aware damage calculation as the forecast";

	battle()->nextTurn(defender->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(defender->defended());
	EXPECT_EQ(battle()->calculateDmgRange(shot).damage.max, baseline);
}
