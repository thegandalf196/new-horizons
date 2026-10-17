/*
 * ProjectedTargetHitTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"

class ProjectedTargetHitTest : public ProjectedTargetHitFixture {};

TEST_F(ProjectedTargetHitTest, PlainRequestsClampBeforeApplyingHealthWithoutTokenOrDefend)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const int64_t request : {-1, 0, 37})
	{
		resetChild();
		auto target = child->getForUpdate(incoming->unitId());
		const auto before = target->getAvailableHealth();
		const auto prepared = projectedTargetHit::prepare(*child, *target, request, context());
		EXPECT_EQ(prepared.damageToApply, std::max<int64_t>(0, request));
		EXPECT_FALSE(prepared.lastStandTriggered);
		EXPECT_EQ(target->getAvailableHealth(), before);
		EXPECT_FALSE(child->armorerLastStandUsed(BattleSide::DEFENDER));
		auto damage = prepared.damageToApply;
		target->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
		EXPECT_FALSE(projectedTargetHit::finishAfterDefiant(*child, *target, prepared));
		EXPECT_EQ(target->getAvailableHealth(), before - std::max<int64_t>(0, request));
		EXPECT_FALSE(target->armorerLastStandDefending);
	}
}

TEST_F(ProjectedTargetHitTest, ReactiveBraceClassificationStopsWithoutDamageRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto target = child->getForUpdate(incoming->unitId());
	const auto before = target->getAvailableHealth();
	const auto prepared = projectedTargetHit::prepare(*child, *target, before * 10, context(false, true));
	ASSERT_TRUE(prepared.lastStandTriggered);
	EXPECT_FALSE(prepared.context.damageRetaliation);
	EXPECT_TRUE(prepared.context.reactiveForLastStand);
	EXPECT_TRUE(child->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_EQ(target->getAvailableHealth(), before) << "Preparation precedes injury";
	EXPECT_FALSE(target->armorerLastStandDefending) << "Defend must wait until after Defiant";
	EXPECT_FALSE(target->armorerLastStandEndedActivation);
	auto damage = prepared.damageToApply;
	target->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(target->getAvailableHealth(), 1);
	EXPECT_FALSE(target->armorerLastStandDefending);
	EXPECT_TRUE(projectedTargetHit::finishAfterDefiant(*child, *target, prepared));
	EXPECT_TRUE(target->defended());
	EXPECT_TRUE(target->armorerLastStandDefending);
	EXPECT_TRUE(target->armorerLastStandEndedActivation);
}

TEST_F(ProjectedTargetHitTest, NonreactivePhysicalHitDefendsButDoesNotEndCurrentActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto target = child->getForUpdate(incoming->unitId());
	const auto prepared = projectedTargetHit::prepare(*child, *target,
		target->getAvailableHealth() * 10, context(false, false));
	ASSERT_TRUE(prepared.lastStandTriggered);
	auto damage = prepared.damageToApply;
	target->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_FALSE(projectedTargetHit::finishAfterDefiant(*child, *target, prepared));
	EXPECT_TRUE(target->armorerLastStandDefending);
	EXPECT_FALSE(target->armorerLastStandEndedActivation);
}

TEST_F(ProjectedTargetHitTest, ReactiveHitOnInactiveVictimDoesNotEndAnotherUnitsActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	activate(braced->unitId());
	resetChild();
	auto target = child->getForUpdate(incoming->unitId());
	const auto prepared = projectedTargetHit::prepare(*child, *target,
		target->getAvailableHealth() * 10, context());
	ASSERT_TRUE(prepared.lastStandTriggered);
	auto damage = prepared.damageToApply;
	target->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_FALSE(projectedTargetHit::finishAfterDefiant(*child, *target, prepared));
	EXPECT_FALSE(target->armorerLastStandEndedActivation);
}

TEST_F(ProjectedTargetHitTest, SpentTokenCannotBeGrantedAgainByThePreparedHit)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_FALSE(child->battleGetFightingHero(BattleSide::DEFENDER));
	ASSERT_EQ(child->battleGetOwnerHero(child->battleGetUnitByID(incoming->unitId())), defenderSideHero);
	child->consumeArmorerLastStand(BattleSide::DEFENDER);
	EXPECT_THROW(child->consumeArmorerLastStand(BattleSide::DEFENDER), std::runtime_error);
	EXPECT_THROW(child->consumeArmorerLastStand(BattleSide::ATTACKER), std::runtime_error);
	auto target = child->getForUpdate(incoming->unitId());
	const auto request = target->getAvailableHealth() * 10;
	const auto prepared = projectedTargetHit::prepare(*child, *target, request, context());
	EXPECT_FALSE(prepared.lastStandTriggered);
	EXPECT_EQ(prepared.damageToApply, request);
	auto damage = prepared.damageToApply;
	target->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_FALSE(target->alive());
	EXPECT_FALSE(projectedTargetHit::finishAfterDefiant(*child, *target, prepared));
}

TEST_F(ProjectedTargetHitTest, GuardianSpiritPrecedesLastStandHealthCapExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto target = child->getForUpdate(incoming->unitId());
	target->guardianSpiritHitPoints = 50;
	target->guardianSpiritRoundsRemaining = 2;
	const auto before = target->getAvailableHealth();
	const auto prepared = projectedTargetHit::prepare(*child, *target, before * 10, context());
	ASSERT_TRUE(prepared.lastStandTriggered);
	EXPECT_EQ(prepared.damageToApply, before + 50 - 1);
	auto damage = prepared.damageToApply;
	target->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(target->getAvailableHealth(), 1);
	EXPECT_EQ(target->guardianSpiritHitPoints, 0);
	EXPECT_TRUE(projectedTargetHit::finishAfterDefiant(*child, *target, prepared));
}

TEST_F(ProjectedTargetHitTest, BranchPhasesDoNotMutateParentSiblingLiveRngLedgerOrOrders)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto parent = child;
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto original = incoming->acquireState()->save().toString();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto ledger = projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::DEFENDER));
	const auto orders = projectedHitTest::serialized(battle()->getHeroOrderStates(BattleSide::DEFENDER));
	const auto traffic = server.startedActions.size();
	auto target = child->getForUpdate(incoming->unitId());
	const auto prepared = projectedTargetHit::prepare(*child, *target,
		target->getAvailableHealth() * 10, context());
	auto damage = prepared.damageToApply;
	target->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_TRUE(projectedTargetHit::finishAfterDefiant(*child, *target, prepared));
	for(const auto & other : {parent, sibling})
	{
		EXPECT_EQ(other->battleGetUnitByID(incoming->unitId())->acquireState()->save().toString(), original);
		EXPECT_FALSE(other->armorerLastStandUsed(BattleSide::DEFENDER));
	}
	EXPECT_EQ(incoming->acquireState()->save().toString(), original);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::DEFENDER)), ledger);
	EXPECT_EQ(projectedHitTest::serialized(battle()->getHeroOrderStates(BattleSide::DEFENDER)), orders);
	EXPECT_EQ(server.startedActions.size(), traffic);
}
