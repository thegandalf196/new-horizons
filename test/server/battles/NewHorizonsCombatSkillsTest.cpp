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
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

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

	void setArcheryPerk(CGHeroInstance * hero, std::string_view perk,
		MasteryLevel::Type rank = MasteryLevel::BASIC)
	{
		const int decoded = SecondarySkill::decode(std::string(newHorizonsArchery::SKILL));
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
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
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK,
		BonusSource::OTHER, 5, BonusSourceID()));
	auto & fortune = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	fortune.perfectMoment = true;
	ASSERT_TRUE(battle()->battleCanUsePerfectMoment(shooter, target, true));

	BattleAction action = BattleAction::makeMeleeAttack(shooter, target->getPosition(), *nonAdjacentFiringHex, false);
	action.archerySkirmisherAttack = true;
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
	stateCopy->archeryDeadeyeRound = battle()->battleGetRound();
	stateCopy->archerySuppressionActivationSerial = 7;
	stateCopy->archeryRainOfArrowsActivationSerial = 9;
	stateCopy->archeryRecordCrossfireDamage(BattleSide::ATTACKER, 1001, battle()->battleGetRound());
	stateCopy->archeryRecordCrossfireDamage(BattleSide::DEFENDER, 1002, battle()->battleGetRound());
	const JsonNode saved = stateCopy->save();
	stateCopy->archeryCounterfireRound = -1;
	stateCopy->archeryDeadeyeRound = -1;
	stateCopy->archerySuppressionActivationSerial = -1;
	stateCopy->archeryRainOfArrowsActivationSerial = -1;
	stateCopy->archeryCrossfireRound = -1;
	stateCopy->archeryCrossfireAttackers.clear();
	stateCopy->archeryCrossfireDefenders.clear();
	ASSERT_NO_THROW(stateCopy->load(saved));
	EXPECT_EQ(stateCopy->archeryCounterfireRound, battle()->battleGetRound());
	EXPECT_EQ(stateCopy->archeryDeadeyeRound, battle()->battleGetRound());
	EXPECT_EQ(stateCopy->archerySuppressionActivationSerial, 7);
	EXPECT_EQ(stateCopy->archeryRainOfArrowsActivationSerial, 9);
	EXPECT_TRUE(stateCopy->archeryCrossfireAvailable(BattleSide::ATTACKER, 2001, battle()->battleGetRound()));
	EXPECT_TRUE(stateCopy->archeryCrossfireAvailable(BattleSide::DEFENDER, 2002, battle()->battleGetRound()));
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
	setArcheryPerk(defenderSideHero, newHorizonsArchery::CROSSFIRE, MasteryLevel::ADVANCED);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:marksman"), BattleHex(rightHex + 4), 10);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:marksman"), BattleHex(rightHex + 5), 10);
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
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Crossfire adds +15% ranged damage") != std::string::npos;
	})) << "The second friendly Counterfire shot can use the first shot's positive damage provenance";
}

TEST_F(NewHorizonsCombatSkillsTest, ArmorPiercingAndHighArcModifyOnlyTheirAuthoredRangedTerms)
{
	startGame();
	attackerSideHero->setSecSkillLevel(skill("new-horizons:archery"), MasteryLevel::ADVANCED,
		ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
	auto * distantTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(15, 5), 100);
	auto * adjacentTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(4, 5), 100);
	forceMaximumDamage(shooter);
	const BattleAttackInfo distantShot(shooter, distantTarget, 0, true);
	const BattleAttackInfo adjacentShot(shooter, adjacentTarget, 0, true);
	const auto distantBasic = battle()->calculateDmgRange(distantShot).damage.max;
	const auto adjacentBasic = battle()->calculateDmgRange(adjacentShot).damage.max;
	setArcheryPerk(attackerSideHero, newHorizonsArchery::ARMOR_PIERCING_SHOT, MasteryLevel::ADVANCED);
	const auto piercing = battle()->calculateDmgRange(distantShot);
	EXPECT_EQ(piercing.archeryDefenseIgnorePercent, newHorizonsArchery::ARMOR_PIERCING_DEFENSE_IGNORE_PERCENT);
	EXPECT_GT(piercing.damage.max, distantBasic);
	const auto adjacentWithoutHighArc = battle()->calculateDmgRange(adjacentShot).damage.max;
	setArcheryPerk(attackerSideHero, newHorizonsArchery::HIGH_ARC, MasteryLevel::ADVANCED);
	const auto highArcDistant = battle()->calculateDmgRange(distantShot);
	const auto highArcAdjacent = battle()->calculateDmgRange(adjacentShot);
	EXPECT_TRUE(highArcDistant.archeryHighArc);
	EXPECT_GT(highArcDistant.damage.max, piercing.damage.max)
		<< "High Arc halves the ordinary far-range penalty in addition to Armor-Piercing Shot";
	EXPECT_TRUE(highArcAdjacent.archeryHighArc);
	EXPECT_EQ(highArcAdjacent.damage.max, adjacentWithoutHighArc)
		<< "High Arc does not erase the separate adjacent-target penalty";
	EXPECT_GT(adjacentBasic, 0);

	const auto * turret = addStack(BattleSide::ATTACKER, CreatureID::ARROW_TOWERS, BattleHex(1, 1), 1);
	EXPECT_FALSE(newHorizonsArchery::isOrdinaryPhysicalShooter(turret));
	EXPECT_FALSE(newHorizonsArchery::isOrdinaryPhysicalShooter(
		addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(2, 7), 1)));
}

TEST_F(NewHorizonsCombatSkillsTest, CrossfireRecognizesEarlierPositiveMeleeDamageByAnotherShooter)
{
	startGame();
	attackerSideHero->setSecSkillLevel(skill("new-horizons:archery"), MasteryLevel::ADVANCED,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL),
		std::string(newHorizonsArchery::CROSSFIRE)});
	ASSERT_TRUE(newHorizonsArchery::hasCrossfire(attackerSideHero));
	startBattle();
	auto * meleeShooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(8, 5), 20);
	auto * rangedShooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 5), 20);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(9, 5), 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != meleeShooter && unit != rangedShooter && unit != target)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(meleeShooter);
	forceMaximumDamage(rangedShooter);
	beginCombat();

	battle()->activeStack = meleeShooter->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(meleeShooter, target->getPosition(), meleeShooter->getPosition(), false)));
	EXPECT_TRUE(target->acquireState()->archeryCrossfireAvailable(BattleSide::ATTACKER,
		rangedShooter->unitId(), battle()->battleGetRound()));

	BattleAttackInfo forecast(rangedShooter, target, 0, true);
	const auto boosted = battle()->calculateDmgRange(forecast);
	EXPECT_EQ(boosted.archeryCrossfireDamagePercent, newHorizonsArchery::CROSSFIRE_DAMAGE_PERCENT);
	battle()->activeStack = rangedShooter->unitId();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(rangedShooter, target)));
	const auto shot = std::ranges::find_if(server.attacks, [rangedShooter](const BattleAttack & attack)
		{ return attack.stackAttacking == rangedShooter->unitId() && !attack.counter(); });
	ASSERT_NE(shot, server.attacks.end());
	const auto hit = std::ranges::find(shot->bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, shot->bsa.end());
	EXPECT_EQ(hit->damageAmount, boosted.damage.max);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Crossfire adds +15% ranged damage") != std::string::npos;
	}));
}

TEST_F(NewHorizonsCombatSkillsTest, DeadeyeIsSpentByTheFirstRangedStrikeIncludingCounterfire)
{
	startGame();
	setArcheryPerk(attackerSideHero, newHorizonsArchery::DEADEYE, MasteryLevel::EXPERT);
	setArcheryPerk(defenderSideHero, newHorizonsArchery::DEADEYE, MasteryLevel::EXPERT);
	setArcheryPerk(defenderSideHero, newHorizonsArchery::COUNTERFIRE, MasteryLevel::EXPERT);
	setArcheryPerk(defenderSideHero, newHorizonsArchery::ARMOR_PIERCING_SHOT, MasteryLevel::EXPERT);
	setArcheryPerk(defenderSideHero, newHorizonsArchery::HIGH_ARC, MasteryLevel::EXPERT);
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
	const auto firstShotForecast = battle()->calculateDmgRange(BattleAttackInfo(shooter, counterShooter, 0, true));
	EXPECT_TRUE(firstShotForecast.archeryDeadeye);
	EXPECT_EQ(firstShotForecast.archeryDefenseIgnorePercent,
		newHorizonsArchery::DEADEYE_DEFENSE_IGNORE_PERCENT);
	const auto counterfireForecast = battle()->calculateDmgRange(BattleAttackInfo(counterShooter, shooter, 0, true));
	EXPECT_TRUE(counterfireForecast.archeryDeadeye);
	EXPECT_TRUE(counterfireForecast.archeryHighArc);
	EXPECT_EQ(counterfireForecast.archeryDefenseIgnorePercent,
		newHorizonsArchery::ARMOR_PIERCING_DEFENSE_IGNORE_PERCENT
			+ newHorizonsArchery::DEADEYE_DEFENSE_IGNORE_PERCENT)
		<< "Armor-Piercing Shot, High Arc, and Deadeye apply to Counterfire under the owning hero";
	battle()->activeStack = shooter->unitId();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, counterShooter)));
	EXPECT_EQ(shooter->acquireState()->archeryDeadeyeRound, round);
	EXPECT_EQ(counterShooter->acquireState()->archeryDeadeyeRound, round)
		<< "Counterfire spends the reacting shooter's Deadeye even though it is not that stack's activation";
	EXPECT_FALSE(battle()->calculateDmgRange(BattleAttackInfo(counterShooter, shooter, 0, true)).archeryDeadeye)
		<< "The reacting stack's later shot in this round cannot spend Deadeye twice";
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_TRUE(server.attacks.back().counter());
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Deadeye rolls maximum creature damage") != std::string::npos;
	}));
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Armor-Piercing Shot ignores 20%") != std::string::npos;
	}));
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("High Arc ignores obstacle penalties") != std::string::npos;
	}));
}

TEST_F(NewHorizonsCombatSkillsTest, SuppressionTriggersOnFirstPositiveHitOnlyOncePerActivation)
{
	startGame();
	setArcheryPerk(attackerSideHero, newHorizonsArchery::SUPPRESSION, MasteryLevel::ADVANCED);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 20);
	const BattleHex primaryHex(rightHex + 4);
	BattleHex collateralHex = BattleHex::INVALID;
	for(int rawHex = 0; rawHex < GameConstants::BFIELD_SIZE; ++rawHex)
	{
		const BattleHex candidate(rawHex);
		if(candidate.isValid() && BattleHex::getDistance(candidate, primaryHex) == 1
			&& !battle()->battleGetStackByPos(candidate))
		{
			collateralHex = candidate;
			break;
		}
	}
	ASSERT_TRUE(collateralHex.isValid());
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), collateralHex, 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), primaryHex, 100);
	const Bonus splash(BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID());
	shooter->addNewBonus(std::make_shared<Bonus>(splash));
	ASSERT_LT(collateral->unitId(), target->unitId()) << "The collateral is deliberately enumerated before the primary";
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != collateral && unit != target)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(shooter);
	beginCombat();
	battle()->activeStack = shooter->unitId();
	ASSERT_GT(shooter->getTotalAttacks(true), 1);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, target)));
	const auto speedPenalties = target->getAllBonuses(Selector::type()(BonusType::STACKS_SPEED));
	ASSERT_TRUE(speedPenalties);
	EXPECT_EQ(std::ranges::count_if(*speedPenalties, [](const auto & bonus)
		{ return bonus->source == BonusSource::OTHER && bonus->duration == BonusDuration::STACK_GETS_TURN && bonus->val == -1; }), 1);
	const auto collateralPenalties = collateral->getAllBonuses(Selector::type()(BonusType::STACKS_SPEED));
	ASSERT_TRUE(collateralPenalties);
	EXPECT_FALSE(std::ranges::any_of(*collateralPenalties, [](const auto & bonus)
		{ return bonus->source == BonusSource::OTHER && bonus->duration == BonusDuration::STACK_GETS_TURN && bonus->val == -1; }))
		<< "Server processes the primary hit before collateral regardless of unit ID";
	EXPECT_EQ(shooter->acquireState()->archerySuppressionActivationSerial,
		static_cast<int32_t>(battle()->getBattle()->getActivationSerial()));
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Suppression reduces") != std::string::npos;
	}));
}

TEST_F(NewHorizonsCombatSkillsTest, RainOfArrowsSumsActualMultishotDamageAndChoosesHighestPostHitHealth)
{
	startGame();
	setArcheryPerk(attackerSideHero, newHorizonsArchery::RAIN_OF_ARROWS, MasteryLevel::EXPERT);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	std::vector<BattleHex> adjacentFree;
	for(const auto & hex : primary->getSurroundingHexes())
		if(hex.isValid() && !battle()->battleGetStackByPos(hex))
			adjacentFree.push_back(hex);
	ASSERT_GE(adjacentFree.size(), 3u);
	auto * highHealthTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), adjacentFree[0], 8);
	auto * lowHealthTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), adjacentFree[1], 3);
	auto * friendlyTarget = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), adjacentFree[2], 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != primary && unit != highHealthTarget && unit != lowHealthTarget
			&& unit != friendlyTarget)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(shooter);
	beginCombat();
	ASSERT_GT(shooter->getTotalAttacks(true), 1);
	const auto highHealthBefore = highHealthTarget->getAvailableHealth();
	const auto lowHealthBefore = lowHealthTarget->getAvailableHealth();
	const auto friendlyBefore = friendlyTarget->getAvailableHealth();
	battle()->activeStack = shooter->unitId();
	server.attacks.clear();
	server.battleLogLines.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, primary)));
	int64_t actualPrimaryDamage = 0;
	for(const auto & attack : server.attacks)
		if(attack.stackAttacking == shooter->unitId() && attack.shot() && !attack.counter())
			for(const auto & hit : attack.bsa)
				if(hit.stackAttacked == primary->unitId())
					actualPrimaryDamage += hit.damageAmount;
	ASSERT_GT(actualPrimaryDamage, 0);
	const int64_t expectedRainDamage = actualPrimaryDamage * newHorizonsArchery::RAIN_OF_ARROWS_DAMAGE_PERCENT / 100;
	EXPECT_EQ(highHealthBefore - highHealthTarget->getAvailableHealth(), expectedRainDamage);
	EXPECT_EQ(lowHealthBefore - lowHealthTarget->getAvailableHealth(), 0);
	EXPECT_EQ(friendlyBefore - friendlyTarget->getAvailableHealth(), 0)
		<< "Rain of Arrows selects only enemy stacks, even when a friendly has more aggregate HP";
	EXPECT_EQ(shooter->acquireState()->archeryRainOfArrowsActivationSerial,
		static_cast<int32_t>(battle()->getBattle()->getActivationSerial()));
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Rain of Arrows deals") != std::string::npos;
	}));
}

TEST_F(NewHorizonsCombatSkillsTest, RainOfArrowsUsesPrimaryDamageOnlyWhenTheVolleyAlsoHitsCollateral)
{
	startGame();
	setArcheryPerk(attackerSideHero, newHorizonsArchery::RAIN_OF_ARROWS, MasteryLevel::EXPERT);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	std::vector<BattleHex> adjacentFree;
	for(const auto & hex : primary->getSurroundingHexes())
		if(hex.isValid() && !battle()->battleGetStackByPos(hex))
			adjacentFree.push_back(hex);
	ASSERT_GE(adjacentFree.size(), 2u);
	auto * highHealthTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), adjacentFree[0], 30);
	auto * lowHealthTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), adjacentFree[1], 10);
	const Bonus splash(BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID());
	shooter->addNewBonus(std::make_shared<Bonus>(splash));
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != primary && unit != highHealthTarget && unit != lowHealthTarget)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(shooter);
	beginCombat();
	const auto highBefore = highHealthTarget->getAvailableHealth();
	const auto lowBefore = lowHealthTarget->getAvailableHealth();
	battle()->activeStack = shooter->unitId();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, primary)));
	int64_t primaryDamage = 0;
	int64_t highCollateralDamage = 0;
	int64_t lowCollateralDamage = 0;
	for(const auto & attack : server.attacks)
	{
		if(attack.stackAttacking != shooter->unitId() || !attack.shot() || attack.counter())
			continue;
		for(const auto & hit : attack.bsa)
		{
			if(hit.stackAttacked == primary->unitId())
				primaryDamage += hit.damageAmount;
			else if(hit.stackAttacked == highHealthTarget->unitId())
				highCollateralDamage += hit.damageAmount;
			else if(hit.stackAttacked == lowHealthTarget->unitId())
				lowCollateralDamage += hit.damageAmount;
		}
	}
	ASSERT_GT(primaryDamage, 0);
	ASSERT_GT(highCollateralDamage + lowCollateralDamage, 0);
	const int64_t highRainDamage = highBefore - highHealthTarget->getAvailableHealth() - highCollateralDamage;
	const int64_t lowRainDamage = lowBefore - lowHealthTarget->getAvailableHealth() - lowCollateralDamage;
	const int64_t expectedRainDamage = primaryDamage * newHorizonsArchery::RAIN_OF_ARROWS_DAMAGE_PERCENT / 100;
	EXPECT_EQ(highRainDamage, expectedRainDamage);
	EXPECT_EQ(lowRainDamage, 0)
		<< "Collateral damage to adjacent stacks is not added to Rain of Arrows' primary-target basis";
}

TEST_F(NewHorizonsCombatSkillsTest, RainOfArrowsUsesCapturedDoubleWideFootprintAfterPrimaryDies)
{
	startGame();
	setArcheryPerk(attackerSideHero, newHorizonsArchery::RAIN_OF_ARROWS, MasteryLevel::EXPERT);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 100);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:behemoth"), BattleHex(rightHex + 4), 1);
	ASSERT_TRUE(primary->doubleWide());
	const auto originalFootprint = primary->getHexes();
	ASSERT_EQ(originalFootprint.size(), 2u);
	BattleHex rearOnlyAdjacent = BattleHex::INVALID;
	for(int rawHex = 0; rawHex < GameConstants::BFIELD_SIZE; ++rawHex)
	{
		const BattleHex candidate(rawHex);
		if(candidate.isValid() && BattleHex::getDistance(candidate, originalFootprint[1]) == 1
			&& BattleHex::getDistance(candidate, originalFootprint[0]) > 1
			&& !battle()->battleGetStackByPos(candidate))
		{
			rearOnlyAdjacent = candidate;
			break;
		}
	}
	ASSERT_TRUE(rearOnlyAdjacent.isValid()) << "Find an enemy placement adjacent only to the wide stack's rear hex";
	auto * secondary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), rearOnlyAdjacent, 10);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != primary && unit != secondary)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(shooter);
	beginCombat();
	const auto secondaryBefore = secondary->getAvailableHealth();
	battle()->activeStack = shooter->unitId();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, primary)));
	ASSERT_FALSE(primary->alive());
	const auto primaryHit = std::ranges::find_if(server.attacks.front().bsa, [primary](const BattleStackAttacked & hit)
		{ return hit.stackAttacked == primary->unitId(); });
	ASSERT_NE(primaryHit, server.attacks.front().bsa.end());
	EXPECT_EQ(secondaryBefore - secondary->getAvailableHealth(),
		primaryHit->damageAmount * newHorizonsArchery::RAIN_OF_ARROWS_DAMAGE_PERCENT / 100)
		<< "The captured tail footprint still selects a post-hit neighbor after the primary dies";
}

TEST_F(NewHorizonsCombatSkillsTest, RainOfArrowsBreaksEqualHealthTiesByAscendingOccupiedHex)
{
	startGame();
	setArcheryPerk(attackerSideHero, newHorizonsArchery::RAIN_OF_ARROWS, MasteryLevel::EXPERT);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	std::vector<BattleHex> adjacentFree;
	for(const auto & hex : primary->getSurroundingHexes())
		if(hex.isValid() && !battle()->battleGetStackByPos(hex))
			adjacentFree.push_back(hex);
	ASSERT_GE(adjacentFree.size(), 2u);
	std::sort(adjacentFree.begin(), adjacentFree.end(), [](const BattleHex & left, const BattleHex & right)
		{ return left.toInt() < right.toInt(); });
	auto * lowerHex = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), adjacentFree[0], 5);
	auto * higherHex = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), adjacentFree[1], 5);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != primary && unit != lowerHex && unit != higherHex)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(shooter);
	beginCombat();
	const auto lowerBefore = lowerHex->getAvailableHealth();
	const auto higherBefore = higherHex->getAvailableHealth();
	battle()->activeStack = shooter->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, primary)));
	EXPECT_GT(lowerBefore - lowerHex->getAvailableHealth(), 0);
	EXPECT_EQ(higherBefore - higherHex->getAvailableHealth(), 0);
}

TEST_F(NewHorizonsCombatSkillsTest, RainOfArrowsHasNoSecondaryEffectWithoutAnAdjacentEnemy)
{
	startGame();
	setArcheryPerk(attackerSideHero, newHorizonsArchery::RAIN_OF_ARROWS, MasteryLevel::EXPERT);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != primary)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	forceMaximumDamage(shooter);
	beginCombat();
	battle()->activeStack = shooter->unitId();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, primary)));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Rain of Arrows finds no enemy stack adjacent") != std::string::npos;
	}));
	EXPECT_EQ(shooter->acquireState()->archeryRainOfArrowsActivationSerial,
		static_cast<int32_t>(battle()->getBattle()->getActivationSerial()));
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
		BonusSource::SPELL_EFFECT, 80, BonusSourceID(SpellID(SpellID::AIR_SHIELD)),
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
