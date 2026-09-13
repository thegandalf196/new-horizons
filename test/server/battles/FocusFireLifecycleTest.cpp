/*
 * FocusFireLifecycleTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "FocusFireFixture.h"

class FocusFireLifecycleTest : public FocusFireFixture {};

TEST_F(FocusFireLifecycleTest, ValidSpellExcludesFocusFire)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
	attackerSideHero->mana = 100;
	ASSERT_TRUE(submit(heroAction(0)));
	const auto started = server.startedActions.size();
	EXPECT_FALSE(submit(focusAction(target->unitId())));
	EXPECT_EQ(server.startedActions.size(), started);
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
}

TEST_F(FocusFireLifecycleTest, FocusFireExcludesAnOtherwiseLegalSpellWithoutManaCost)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
	attackerSideHero->mana = 100;
	ASSERT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto started = server.startedActions.size();
	EXPECT_FALSE(submit(heroAction(0)));
	EXPECT_EQ(server.startedActions.size(), started);
	EXPECT_EQ(attackerSideHero->mana, 100);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(FocusFireLifecycleTest, SameCreatureDifferentIdAndMovementDoNotTransferMark)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:angel"),
		BattleHex(rightHex + 4 + GameConstants::BFIELD_WIDTH), 100);
	ASSERT_EQ(target->creatureId(), other->creatureId());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, other, true), 0);
	BattleStackMoved move;
	move.battleID = BattleID(0);
	move.stack = target->unitId();
	move.teleporting = true;
	move.tilesToMove.insert(BattleHex(rightHex + 3));
	gameHandler->sendAndApply(move);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 30);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, other, true), 0);
}

TEST_F(FocusFireLifecycleTest, TargetAndIncludedShooterControlLossAndReturnKeepIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto before = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	target->addNewBonus(control);
	ASSERT_EQ(battle()->battleGetOwner(target), PlayerColor(0));
	EXPECT_FALSE(battle()->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 0);
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), before);
	target->removeBonus(control);
	EXPECT_TRUE(battle()->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 30);
	shooter->addNewBonus(control);
	ASSERT_EQ(battle()->battleGetOwner(shooter), PlayerColor(1));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 0);
	shooter->removeBonus(control);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 30);
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), before);
}

TEST_F(FocusFireLifecycleTest, CohortUsesControllerAtIssueNotImmutableOrigin)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	auto * enemyOrigin = addShooter(BattleSide::DEFENDER, BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH));
	auto * ownOrigin = addShooter(BattleSide::ATTACKER, BattleHex(leftHex - 2 * GameConstants::BFIELD_WIDTH));
	auto enemyControl = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	auto ownControl = std::make_shared<Bonus>(*enemyControl);
	enemyOrigin->addNewBonus(enemyControl);
	ownOrigin->addNewBonus(ownControl);
	ASSERT_EQ(battle()->battleGetOwner(enemyOrigin), PlayerColor(0));
	ASSERT_EQ(battle()->battleGetOwner(ownOrigin), PlayerColor(1));
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(enemyOrigin, target, true), 30);
	enemyOrigin->removeBonus(enemyControl);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(enemyOrigin, target, true), 0);
	enemyOrigin->addNewBonus(enemyControl);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(enemyOrigin, target, true), 30);
	ownOrigin->removeBonus(ownControl);
	ASSERT_EQ(battle()->battleGetOwner(ownOrigin), PlayerColor(0));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(ownOrigin, target, true), 0);
}

TEST_F(FocusFireLifecycleTest, RemovedTargetRetainsInactiveIdAndNewUnitDoesNotInheritIt)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	const auto id = target->unitId();
	ASSERT_TRUE(submit(focusAction(id)));
	const auto before = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(id, UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	EXPECT_FALSE(battle()->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), before);
	auto * replacement = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	EXPECT_NE(replacement->unitId(), id);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, replacement, true), 0);
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
}

TEST_F(FocusFireLifecycleTest, DeathAndSameIdResurrectionToggleEffectWithoutClearingAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto before = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	auto dead = target->acquireState();
	int64_t damage = dead->getAvailableHealth();
	dead->damage(damage);
	BattleUnitsChanged kill;
	kill.battleID = BattleID(0);
	kill.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
	kill.changedStacks.back().data = dead->save();
	kill.changedStacks.back().healthDelta = -damage;
	gameHandler->sendAndApply(kill);
	ASSERT_FALSE(target->alive());
	EXPECT_FALSE(battle()->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), before);
	auto revived = target->acquireState();
	int64_t healing = damage;
	const auto healed = revived->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_GT(healed.healedHealthPoints, 0);
	BattleUnitsChanged resurrect;
	resurrect.battleID = BattleID(0);
	resurrect.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
	resurrect.changedStacks.back().data = revived->save();
	resurrect.changedStacks.back().healthDelta = healed.healedHealthPoints;
	gameHandler->sendAndApply(resurrect);
	ASSERT_TRUE(target->alive());
	EXPECT_TRUE(battle()->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 30);
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), before);
}
