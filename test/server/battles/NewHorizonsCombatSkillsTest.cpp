/*
 * NewHorizonsCombatSkillsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/battle/NewHorizonsArchery.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"

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

	void setArcheryPerk(CGHeroInstance * hero, std::string_view perk)
	{
		const int decoded = SecondarySkill::decode(std::string(newHorizonsArchery::SKILL));
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({std::string(newHorizonsArchery::SKILL), std::string(perk)});
		ASSERT_TRUE(hero->hasActivePerk(std::string(newHorizonsArchery::SKILL), std::string(perk)));
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

TEST_F(NewHorizonsCombatSkillsTest, PointBlankShotRemovesOnlyTheAdjacentRangedPenaltyAndLogsIt)
{
	startGame();
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	auto * distantTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	forceMaximumDamage(shooter);
	const BattleAttackInfo shot(shooter, target, 0, true);
	const int64_t ordinaryDamage = battle()->calculateDmgRange(shot).damage.max;
	ASSERT_GT(ordinaryDamage, 0);

	EXPECT_FALSE(battle()->battleCanShoot(shooter, target->getPosition()))
		<< "The adjacent target blocks an ordinary shooter before Point-Blank is learned";
	setArcheryPerk(attackerSideHero, newHorizonsArchery::POINT_BLANK_SHOT);
	const int64_t pointBlankDamage = battle()->calculateDmgRange(shot).damage.max;
	EXPECT_EQ(pointBlankDamage, ordinaryDamage * 2)
		<< "The perk removes the ordinary 50% adjacent-shot penalty, without altering other damage factors";
	EXPECT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()))
		<< "Point-Blank bypasses the blocked-shooter gate only for its adjacent target";
	EXPECT_FALSE(battle()->battleCanShoot(shooter, distantTarget->getPosition()))
		<< "Point-Blank cannot bypass the blocked-shooter gate for a distant target";

	// Repeat the gate/penalty contract with the Castle shooter upgrade.
	BattleUnitsChanged replaceShooter;
	replaceShooter.battleID = BattleID(0);
	replaceShooter.changedStacks.emplace_back(shooter->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(replaceShooter);
	auto * marksman = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 10);
	forceMaximumDamage(marksman);
	EXPECT_TRUE(battle()->battleCanShoot(marksman, target->getPosition()));

	beginCombat();
	battle()->activeStack = marksman->unitId();
	server.battleLogLines.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(marksman, target)));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(server.attacks.back().shot()) << "Point-Blank keeps the player's ranged command explicitly ranged";
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Point-Blank Shot ignores the ordinary adjacent-target ranged penalty") != std::string::npos;
	}));
}

TEST_F(NewHorizonsCombatSkillsTest, SkirmisherMovesOnlyToCanonicalHalfSpeedFiringHexAndDealsSeventyFivePercent)
{
	startGame();
	setArcheryPerk(attackerSideHero, newHorizonsArchery::SKIRMISHER);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(3, 5), 10);
	auto * blocker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 5), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != blocker && unit != target)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(shooter);
	beginCombat();

	ASSERT_FALSE(battle()->battleCanShoot(shooter, target->getPosition()));
	const auto legalTargets = battle()->battleGetSkirmisherTargetHexes(shooter);
	EXPECT_TRUE(vstd::contains(legalTargets, target->getPosition()));
	EXPECT_FALSE(vstd::contains(legalTargets, blocker->getPosition()));
	const auto firingHexes = battle()->battleGetSkirmisherAttackFromHexes(shooter, target->getPosition());
	ASSERT_GT(firingHexes.size(), 1u);
	// Exercise an arbitrary player-selected legal endpoint, not an engine-chosen
	// nearest-hex fallback. Pick the farthest legal path endpoint to distinguish it
	// from the old deterministic nearest behavior.
	BattleHex firingHex = BattleHex::INVALID;
	uint32_t farthestDistance = 0;
	for(const BattleHex & candidate : firingHexes)
	{
		const auto [candidatePath, candidateDistance] = battle()->getPath(shooter->getPosition(), candidate, shooter);
		if(!candidatePath.empty() && candidateDistance > farthestDistance)
		{
			farthestDistance = candidateDistance;
			firingHex = candidate;
		}
	}
	ASSERT_TRUE(firingHex.isValid());
	EXPECT_TRUE(battle()->battleCanSkirmisherAttackFromHex(shooter, target->getPosition(), firingHex));
	EXPECT_FALSE(battle()->battleCanSkirmisherAttackFromHex(shooter, blocker->getPosition(), firingHex))
		<< "Skirmisher accepts enemy targets and rejects allied targets";
	const auto [path, distance] = battle()->getPath(shooter->getPosition(), firingHex, shooter);
	ASSERT_FALSE(path.empty());
	ASSERT_LE(distance, shooter->getMovementRange(0) / 2);
	BattleAttackInfo forecast(shooter, target, distance, true);
	forecast.attackerPos = firingHex;
	forecast.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
	const auto expectedDamage = battle()->calculateDmgRange(forecast).damage.max;

	battle()->activeStack = shooter->unitId();
	server.attacks.clear();
	server.battleLogLines.clear();
	BattleAction action = BattleAction::makeMeleeAttack(shooter, target->getPosition(), firingHex, false);
	action.archerySkirmisherAttack = true;
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	ASSERT_EQ(shooter->getPosition(), firingHex);
	ASSERT_FALSE(server.attacks.empty());
	const auto hit = std::ranges::find(server.attacks.back().bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, server.attacks.back().bsa.end());
	EXPECT_EQ(hit->damageAmount, expectedDamage);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Skirmisher lets the shooter move and fire at 75% normal damage") != std::string::npos;
	}));
}

TEST_F(NewHorizonsCombatSkillsTest, MarksmanSkirmisherPreservesMultiShotAndAcceptsPerfectMomentAtFiringHex)
{
	startGame();
	setArcheryPerk(attackerSideHero, newHorizonsArchery::SKIRMISHER);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 5), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != target)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(shooter);
	beginCombat();

	const auto firingHexes = battle()->battleGetSkirmisherAttackFromHexes(shooter, target->getPosition());
	const auto nonAdjacentFiringHex = std::ranges::find_if(firingHexes, [target](const BattleHex & hex)
		{ return !target->getSurroundingHexes().contains(hex); });
	ASSERT_NE(nonAdjacentFiringHex, firingHexes.end());
	const int rangedAttackCount = shooter->getTotalAttacks(true);
	ASSERT_GT(rangedAttackCount, 1) << "Marksmen should exercise ordinary ranged multi-attack behavior";
	const int shotsBefore = shooter->shots.available();
	const auto [path, movementDistance] = battle()->getPath(shooter->getPosition(), *nonAdjacentFiringHex, shooter);
	ASSERT_FALSE(path.empty());
	ASSERT_LE(movementDistance, shooter->getMovementRange(0) / 2);
	BattleAttackInfo shotEstimate(shooter, target, movementDistance, true);
	shotEstimate.attackerPos = *nonAdjacentFiringHex;
	shotEstimate.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
	const auto expectedDamagePerShot = battle()->calculateDmgRange(shotEstimate).damage.max;
	battle()->activeStack = shooter->unitId();
	auto & fortune = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	fortune.perfectMoment = true;
	ASSERT_TRUE(battle()->battleCanUsePerfectMoment(shooter));

	BattleAction action = BattleAction::makeMeleeAttack(shooter, target->getPosition(), *nonAdjacentFiringHex, false);
	action.archerySkirmisherAttack = true;
	action.perfectMoment = true;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(fortune.perfectMomentUsed)
		<< "Perfect Moment is validated and applied as a ranged Skirmisher attack, not melee from its firing hex";
	EXPECT_EQ(shooter->shots.available(), shotsBefore - rangedAttackCount);
	std::vector<const BattleAttack *> skirmisherShots;
	for(const auto & attack : server.attacks)
		if(attack.stackAttacking == shooter->unitId() && attack.shot() && !attack.counter())
			skirmisherShots.push_back(&attack);
	ASSERT_EQ(skirmisherShots.size(), static_cast<size_t>(rangedAttackCount));
	ASSERT_TRUE(skirmisherShots.front()->fortuneState);
	EXPECT_TRUE(skirmisherShots.front()->fortuneState->perfectMomentUsed)
		<< "The first Skirmisher ranged strike carries the Perfect Moment descriptor";
	// Perfect Moment forces the opening attack to be lucky, which changes its
	// damage independently of Skirmisher's ranged damage coefficient. Later
	// attacks should be ordinary 75% ranged strikes.
	for(size_t i = 1; i < skirmisherShots.size(); ++i)
	{
		const auto * shot = skirmisherShots[i];
		const auto hit = std::ranges::find(shot->bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
		ASSERT_NE(hit, shot->bsa.end());
		EXPECT_EQ(hit->damageAmount, expectedDamagePerShot)
			<< "Every non-Perfect-Moment Marksman shot uses the authored 75% Skirmisher coefficient";
	}
}

TEST_F(NewHorizonsCombatSkillsTest, CounterfireAnswersPhysicalRangedDamageOncePerRoundWithoutRecursing)
{
	startGame();
	setArcheryPerk(defenderSideHero, newHorizonsArchery::COUNTERFIRE);
	setArcheryPerk(attackerSideHero, newHorizonsArchery::COUNTERFIRE);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 10);
	auto * counterShooter = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(rightHex + 4), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != counterShooter)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(shooter);
	forceMaximumDamage(counterShooter);
	beginCombat();

	const auto round = battle()->battleGetRound();
	const auto retaliationAvailableBefore = counterShooter->counterAttacks.available();
	ASSERT_GT(retaliationAvailableBefore, 0);
	battle()->activeStack = shooter->unitId();
	server.attacks.clear();
	server.battleLogLines.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, counterShooter)));
	ASSERT_EQ(counterShooter->acquireState()->archeryCounterfireRound, round);
	EXPECT_EQ(counterShooter->counterAttacks.available(), retaliationAvailableBefore)
		<< "Counterfire does not consume a stack's separate normal melee retaliation";
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks[0].stackAttacking, shooter->unitId());
	EXPECT_FALSE(server.attacks[0].counter());
	EXPECT_EQ(server.attacks[1].stackAttacking, counterShooter->unitId());
	EXPECT_TRUE(server.attacks[1].counter());
	ASSERT_FALSE(server.attacks[1].bsa.empty());
	const auto counterfireHit = std::ranges::find(server.attacks[1].bsa, shooter->unitId(),
		&BattleStackAttacked::stackAttacked);
	ASSERT_NE(counterfireHit, server.attacks[1].bsa.end());
	BattleAttackInfo counterfireForecast(counterShooter, shooter, 0, true);
	counterfireForecast.archeryRangedDamageMultiplierPercent = newHorizonsArchery::COUNTERFIRE_DAMAGE_PERCENT;
	EXPECT_EQ(counterfireHit->damageAmount, battle()->calculateDmgRange(counterfireForecast).damage.max);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Counterfire: ") != std::string::npos;
	}));

	// A second shooter can hit the same stack this round, but it must not answer again.
	auto * secondShooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex + 8), 10);
	forceMaximumDamage(secondShooter);
	battle()->activeStack = secondShooter->unitId();
	const auto attacksBefore = server.attacks.size();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(secondShooter, counterShooter)));
	EXPECT_EQ(server.attacks.size(), attacksBefore + 1);
	EXPECT_EQ(counterShooter->counterAttacks.available(), retaliationAvailableBefore);

	// The same stack's ordinary melee retaliation remains available after its
	// distinct Counterfire reaction and is spent only by this actual melee blow.
	auto * meleeAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"),
		counterShooter->getPosition().cloneInDirection(BattleHex::RIGHT), 10);
	forceMaximumDamage(meleeAttacker);
	battle()->activeStack = meleeAttacker->unitId();
	const auto retaliationAttacksBefore = std::ranges::count_if(server.attacks,
		[counterShooter](const BattleAttack & attack)
		{ return attack.counter() && attack.stackAttacking == counterShooter->unitId(); });
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(meleeAttacker, counterShooter->getPosition(),
			meleeAttacker->getPosition(), false)));
	const auto retaliationAttacksAfter = std::ranges::count_if(server.attacks,
		[counterShooter](const BattleAttack & attack)
		{ return attack.counter() && attack.stackAttacking == counterShooter->unitId(); });
	EXPECT_EQ(retaliationAttacksAfter, retaliationAttacksBefore + 1);
	EXPECT_EQ(counterShooter->counterAttacks.available(), std::max(0, retaliationAvailableBefore - 1));
}

TEST_F(NewHorizonsCombatSkillsTest, CounterfireRefusesTimeStoppedIncapacitatedAndFrozenShootersAndRoundTripsStamp)
{
	startGame();
	setArcheryPerk(defenderSideHero, newHorizonsArchery::COUNTERFIRE);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 10);
	auto * counterShooter = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(rightHex + 4), 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != counterShooter)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	beginCombat();
	ASSERT_TRUE(newHorizonsArchery::canUseCounterfire(defenderSideHero, counterShooter));
	const auto expectNoCounterfire = [&](BonusType type, BonusSource source, BonusSourceID sourceId = BonusSourceID())
	{
		const Bonus bonus(BonusDuration::ONE_BATTLE, type, source, 1, sourceId);
		SetStackEffect applied;
		applied.battleID = BattleID(0);
		applied.toAdd.emplace_back(counterShooter->unitId(), std::vector<Bonus>{bonus});
		gameHandler->sendAndApply(applied);
		ASSERT_FALSE(newHorizonsArchery::canUseCounterfire(defenderSideHero, counterShooter));

		auto * incoming = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex + 8), 10);
		battle()->activeStack = incoming->unitId();
		server.attacks.clear();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeShotAttack(incoming, counterShooter)));
		EXPECT_EQ(std::ranges::count_if(server.attacks, [](const BattleAttack & attack)
			{ return attack.counter(); }), 0)
			<< "Time Stop, incapacity, and Stone Gaze suppress Counterfire in authoritative resolution";
		EXPECT_EQ(counterShooter->acquireState()->archeryCounterfireRound, -1);

		BattleUnitsChanged removeIncoming;
		removeIncoming.battleID = BattleID(0);
		removeIncoming.changedStacks.emplace_back(incoming->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removeIncoming);
		SetStackEffect removed;
		removed.battleID = BattleID(0);
		removed.toRemove.emplace_back(counterShooter->unitId(), std::vector<Bonus>{bonus});
		gameHandler->sendAndApply(removed);
		ASSERT_TRUE(newHorizonsArchery::canUseCounterfire(defenderSideHero, counterShooter));
	};
	expectNoCounterfire(BonusType::TIME_STOP, BonusSource::SPELL_EFFECT);
	expectNoCounterfire(BonusType::NOT_ACTIVE, BonusSource::SPELL_EFFECT);
	expectNoCounterfire(BonusType::NOT_ACTIVE, BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::STONE_GAZE)));
	EXPECT_TRUE(shooter->alive());

	// Round stamp persistence is a serialization concern; use a detached state
	// copy here without affecting authoritative eligibility or combat.
	auto stateCopy = counterShooter->acquireState();
	stateCopy->archeryCounterfireRound = battle()->battleGetRound();
	const JsonNode saved = stateCopy->save();
	stateCopy->archeryCounterfireRound = -1;
	ASSERT_NO_THROW(stateCopy->load(saved));
	EXPECT_EQ(stateCopy->archeryCounterfireRound, battle()->battleGetRound());
}

TEST(NewHorizonsArcheryActionWireTest, ExplicitSkirmisherIntentRoundTripsAndOldProtocolRejectsIt)
{
	BattleAction outgoing;
	outgoing.side = BattleSide::ATTACKER;
	outgoing.actionType = EActionType::WALK_AND_ATTACK;
	outgoing.archerySkirmisherAttack = true;
	outgoing.aimToHex(BattleHex(70));
	outgoing.aimToHex(BattleHex(71));
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & outgoing);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.archerySkirmisherAttack);
	EXPECT_EQ(restored.target[0].hexValue, BattleHex(70));
	EXPECT_EQ(restored.target[1].hexValue, BattleHex(71));

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_IRON_DISCIPLINE;
	EXPECT_THROW(old.oser & outgoing, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST_F(NewHorizonsCombatSkillsTest, CounterfireAnswersEachDamagedStackInOnePhysicalRangedCommand)
{
	startGame();
	setArcheryPerk(defenderSideHero, newHorizonsArchery::COUNTERFIRE);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 10);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 10);
	auto allAdjacent = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID());
	shooter->addNewBonus(allAdjacent);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != primary && unit != collateral)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	beginCombat();
	battle()->activeStack = shooter->unitId();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, primary)));
	const auto counterfireAttacks = std::ranges::count_if(server.attacks, [](const BattleAttack & attack)
		{ return attack.counter(); });
	EXPECT_EQ(counterfireAttacks, 2)
		<< "Both directly hit and collateral enemy stacks answer once; their shots do not recurse";
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
