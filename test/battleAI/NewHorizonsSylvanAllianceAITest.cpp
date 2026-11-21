/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/SylvanTacticsProjection.h"
#include "../../AI/BattleAI/GnollHarriersProjection.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsSylvanTacticsV2.h"
#include "../../lib/battle/NewHorizonsSylvanTacticsRevision.h"

class NewHorizonsSylvanAllianceAITest : public ProjectedTargetHitFixture
{
protected:
	CStack * source = nullptr;
	CStack * victim = nullptr;
	CStack * neighbor = nullptr;
	virtual int policy() const { return 2; }
	void mapLoaded(CMap * loaded) override
	{
		ProjectedTargetHitFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		rules["sylvanTacticsPolicy"].Integer() = policy();
		if(policy() == 2)
			for(auto & row : rules["skills"]["new-horizons:sylvanLuck"]["perks"].Vector())
			{
				const auto parameter = [&](const char * field, int value)
				{
					row["effect"]["parameters"] = JsonNode();
					row["effect"]["parameters"][field] = JsonNode(value);
				};
				const auto & id = row["id"].String();
				if(id.ends_with(".forestSFavor")) parameter("maximumNegativeSpellRounds", 1);
				if(id.ends_with(".serendipity")) parameter("retaliationsConsumed", 1);
				if(id.ends_with(".luckyRecovery")) parameter("attackDamageReductionPercent", 20);
				if(id.ends_with(".natureSProvidence")) parameter("enemySpellCostBonus", 2);
			}
		if(policy() <= 3)
			// Captured policy-2/3 rows keep their own Hunting Pack payload.
			for(auto & row : rules["skills"]["new-horizons:sylvanLuck"]["perks"].Vector())
				if(row["id"].String().ends_with(".elvenPrecision"))
					row["effect"]["parameters"]["attackDamagePercent"].Integer() = 25;
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
	}
	void prepareAlliance(const std::string & creature, const std::string & perk, int rank = 1)
	{
		startGame();
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		if(rank > 1)
			attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck.forestSFavor"});
		attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck." + perk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck." + perk));
		ASSERT_EQ(newHorizonsSylvanTacticsV2::enabled(attackerSideHero), policy() == 2);
		startBattle();
		source = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), 10);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(10, 5), 100);
		neighbor = addStack(BattleSide::ATTACKER, creatureByName("core:battleDwarf"), BattleHex(9, 5), 1);
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		resetChild();
	}
	int64_t projectedHit(bool shooting, int64_t amount)
	{
		auto actor = child->getForUpdate(source->unitId());
		auto target = child->getForUpdate(victim->unitId());
		BattleAttackInfo attack(actor.get(), target.get(), 0, shooting);
		sylvanTacticsAI::StrikeProjection projection(child.get());
		projection.damage(*child, attack, *target, amount, battle::DamageProvenance::PHYSICAL_CREATURE);
		projection.finish(actor->unitId());
		return amount;
	}
};

TEST_F(NewHorizonsSylvanAllianceAITest, OnlyEligibleElfShotNeedsCounterProjection)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:woodElf", "serendipity"));
	auto actor = child->getForUpdate(source->unitId());
	auto target = child->getForUpdate(victim->unitId());
	BattleAttackInfo shot(actor.get(), target.get(), 0, true);
	EXPECT_TRUE(sylvanTacticsAI::needsProjection(*child, shot));
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child)); // No retired v1 history branch.
	BattleAttackInfo melee(actor.get(), target.get(), 0, false);
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child, melee));
	BattleAttackInfo wrongLine(target.get(), actor.get(), 0, true);
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child, wrongLine));
	target->counterAttacks.use();
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child, shot));
}

TEST_F(NewHorizonsSylvanAllianceAITest, DamagingShotSpendsOnlyCopiedFiniteCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:grandElf", "serendipity"));
	const auto liveBefore = projectedHitTest::serialized(*victim);
	ASSERT_EQ(victim->counterAttacks.available(), 1);
	ASSERT_GT(projectedHit(true, 10), 0);
	EXPECT_EQ(child->getForUpdate(victim->unitId())->counterAttacks.available(), 0);
	EXPECT_EQ(projectedHitTest::serialized(*victim), liveBefore);
	EXPECT_EQ(victim->counterAttacks.available(), 1);
	// A second hit cannot consume a negative allowance or mutate the parent.
	ASSERT_GT(projectedHit(true, 10), 0);
	EXPECT_EQ(child->getForUpdate(victim->unitId())->counterAttacks.available(), 0);
	EXPECT_EQ(projectedHitTest::serialized(*victim), liveBefore);
}

TEST_F(NewHorizonsSylvanAllianceAITest, ZeroDamageMeleeAndUnlimitedRetaliationsAreUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:woodElf", "serendipity"));
	EXPECT_EQ(projectedHit(true, 0), 0);
	EXPECT_EQ(child->getForUpdate(victim->unitId())->counterAttacks.available(), 1);
	ASSERT_GT(projectedHit(false, 10), 0);
	EXPECT_EQ(child->getForUpdate(victim->unitId())->counterAttacks.available(), 1);
	const Bonus unlimited(BonusDuration::ONE_BATTLE, BonusType::UNLIMITED_RETALIATIONS,
		BonusSource::OTHER, 1, BonusSourceID());
	child->addUnitBonus(victim->unitId(), {unlimited});
	auto actor = child->getForUpdate(source->unitId());
	auto target = child->getForUpdate(victim->unitId());
	BattleAttackInfo shot(actor.get(), target.get(), 0, true);
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child, shot));
	ASSERT_GT(projectedHit(true, 10), 0);
	child->removeUnitBonus(victim->unitId(), {unlimited});
	EXPECT_EQ(target->counterAttacks.available(), 1);
}

TEST_F(NewHorizonsSylvanAllianceAITest, DestroyedCloneUsesPostShieldHitReceiptInsteadOfZeroCasualtyDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:woodElf", "serendipity"));
	const auto liveBefore = projectedHitTest::serialized(*victim);
	auto actor = child->getForUpdate(source->unitId());
	auto target = child->getForUpdate(victim->unitId());
	target->cloned = true;
	BattleAttackInfo shot(actor.get(), target.get(), 0, true);
	int64_t amount = 10;
	int64_t postShield = -1;
	sylvanTacticsAI::StrikeProjection projection(child.get());
	projection.damage(*child, shot, *target, amount, battle::DamageProvenance::PHYSICAL_CREATURE, &postShield);
	EXPECT_EQ(amount, 0);
	EXPECT_EQ(postShield, 10);
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(target->counterAttacks.available(), 0);
	EXPECT_EQ(projectedHitTest::serialized(*victim), liveBefore);
	EXPECT_EQ(victim->counterAttacks.available(), 1);
	// Callers without a receipt pointer must use the same pre-casualty result.
	resetChild();
	child->getForUpdate(victim->unitId())->cloned = true;
	EXPECT_EQ(projectedHit(true, 10), 0);
	EXPECT_EQ(child->getForUpdate(victim->unitId())->counterAttacks.available(), 0);
	EXPECT_EQ(projectedHitTest::serialized(*victim), liveBefore);
}

TEST_F(NewHorizonsSylvanAllianceAITest, SiblingShotBranchesDoNotShareSpentCounters)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:woodElf", "serendipity"));
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	ASSERT_GT(projectedHit(true, 10), 0);
	EXPECT_EQ(child->getForUpdate(victim->unitId())->counterAttacks.available(), 0);
	EXPECT_EQ(sibling->getForUpdate(victim->unitId())->counterAttacks.available(), 1);
	EXPECT_EQ(victim->counterAttacks.available(), 1);
}

TEST_F(NewHorizonsSylvanAllianceAITest, ShotPreviewAndSelectedReplayAgreeWithoutChangingLiveCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:woodElf", "serendipity"));
	const auto liveBefore = projectedHitTest::serialized(*victim);
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(source, victim, 0, true),
		source->getPosition(), cache, child);
	ASSERT_TRUE(preview.effectPreview);
	EXPECT_EQ(preview.effectPreview->getForUpdate(victim->unitId())->counterAttacks.available(), 0);
	EXPECT_EQ(child->getForUpdate(victim->unitId())->counterAttacks.available(), 1);
	BattleExchangeVariant exchange;
	exchange.trackAttack(preview, child, cache);
	EXPECT_EQ(child->getForUpdate(victim->unitId())->counterAttacks.available(), 0);
	EXPECT_EQ(projectedHitTest::serialized(*victim), liveBefore);
}

TEST_F(NewHorizonsSylvanAllianceAITest, HuntingPackCacheTracksOnlyAffectedDirectedPairsAndExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:centaur", "elvenPrecision"));
	auto actor = child->getForUpdate(source->unitId());
	auto target = child->getForUpdate(victim->unitId());
	ASSERT_TRUE(sylvanTacticsAI::allianceDamage(*child, actor.get(), target.get()));
	EXPECT_FALSE(sylvanTacticsAI::allianceDamage(*child, target.get(), actor.get()));
	EXPECT_FALSE(sylvanTacticsAI::allianceDamage(*child, neighbor, target.get()));
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child, BattleAttackInfo(actor.get(), target.get(), 0, false)));
	DamageCache root;
	root.cacheDamage(actor.get(), target.get(), child);
	const auto protectedSnapshot = root.captureSnapshot();
	const auto enhanced = root.getDamage(actor.get(), target.get(), child);
	EXPECT_TRUE(root.matchesSnapshot(protectedSnapshot));
	child->moveUnit(neighbor->unitId(), BattleHex(3, 7));
	ASSERT_FALSE(sylvanTacticsAI::allianceDamage(*child, actor.get(), target.get()));
	DamageCache continuation(&root);
	const BattleAttackInfo attack(actor.get(), target.get(), 0, false);
	EXPECT_EQ(continuation.getDamage(actor.get(), target.get(), child), child->battleExpectedLuckDamage(attack));
	EXPECT_LT(continuation.getDamage(actor.get(), target.get(), child), enhanced);
	EXPECT_TRUE(root.matchesSnapshot(protectedSnapshot));
	EXPECT_EQ(neighbor->getPosition(), BattleHex(9, 5));
}

class NewHorizonsSylvanHybridAITest : public NewHorizonsSylvanAllianceAITest
{
protected:
	int policy() const override { return 3; }
};

TEST_F(NewHorizonsSylvanHybridAITest, PatientHunterUsesWaitWindowWithoutRetiredElfCounterProjection)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:woodElf", "serendipity"));
	auto actor = child->getForUpdate(source->unitId());
	auto target = child->getForUpdate(victim->unitId());
	const BattleAttackInfo shot(actor.get(), target.get(), 0, true);
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child, shot));
	EXPECT_TRUE(sylvanTacticsAI::dynamicDamage(*child, actor.get(), target.get()));
	const auto ordinary = child->battleExpectedLuckDamage(shot);
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	child->makeWait(actor.get());
	EXPECT_GT(child->battleExpectedLuckDamage(shot), ordinary);
	EXPECT_EQ(source->battlecraftWaitBonusAvailable(), false);
	EXPECT_GT(projectedHit(true, 10), 0);
	EXPECT_EQ(target->counterAttacks.available(), 1);
}

TEST_F(NewHorizonsSylvanHybridAITest, DefendRedeploymentAndFleetfootRemainIsolatedWithoutStrikeBranches)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:centaur", "natureSProvidence", 2));
	auto actor = child->getForUpdate(source->unitId());
	const auto before = child->getSylvanLuckState(BattleSide::ATTACKER);
	const auto liveState = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	const auto fullSpeed = actor->getMovementRange();
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child));
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), child);
	auto branchActor = sibling->getForUpdate(source->unitId());
	sibling->nextTurn(branchActor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	sibling->completeNormalActivation(branchActor->unitId());
	branchActor->defending = true;
	sibling->nextTurn(branchActor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(branchActor->getMovementRange(), fullSpeed + 1); // +2 redeployment, spent +1 Fleetfoot.
	EXPECT_EQ(branchActor->getMovementRange(1), fullSpeed - 1);
	EXPECT_EQ(child->getSylvanLuckState(BattleSide::ATTACKER), before);
	EXPECT_EQ(battle()->getSylvanLuckState(BattleSide::ATTACKER), liveState);
}

TEST_F(NewHorizonsSylvanHybridAITest, DappledCoverRefreshesCachedRangedDamageAfterWaitingEnds)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:centaur", "luckyRecovery"));
	auto * liveShooter = addStack(BattleSide::DEFENDER, creatureByName("core:woodElf"), BattleHex(10, 7), 10);
	resetChild();
	auto actor = child->getForUpdate(source->unitId());
	auto shooter = child->getForUpdate(liveShooter->unitId());
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	child->makeWait(actor.get());
	ASSERT_TRUE(sylvanTacticsAI::dynamicDamage(*child, shooter.get(), actor.get()));
	DamageCache root;
	root.cacheDamage(shooter.get(), actor.get(), child);
	const auto protectedDamage = root.getDamage(shooter.get(), actor.get(), child);
	const auto snapshot = root.captureSnapshot();
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	DamageCache continuation(&root);
	EXPECT_GT(continuation.getDamage(shooter.get(), actor.get(), child), protectedDamage);
	EXPECT_TRUE(root.matchesSnapshot(snapshot));
	EXPECT_FALSE(source->waited());
}

TEST_F(NewHorizonsSylvanHybridAITest, HuntingPackCacheTracksOnlyAffectedPairsAndSupportMovement)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:centaurCaptain", "elvenPrecision"));
	auto actor = child->getForUpdate(source->unitId());
	auto target = child->getForUpdate(victim->unitId());
	ASSERT_TRUE(sylvanTacticsAI::allianceDamage(*child, actor.get(), target.get()));
	EXPECT_FALSE(sylvanTacticsAI::allianceDamage(*child, target.get(), actor.get()));
	EXPECT_FALSE(sylvanTacticsAI::dynamicDamage(*child, target.get(), actor.get()));
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child));
	DamageCache root;
	root.cacheDamage(actor.get(), target.get(), child);
	const auto snapshot = root.captureSnapshot();
	const auto enhanced = root.getDamage(actor.get(), target.get(), child);
	child->moveUnit(neighbor->unitId(), BattleHex(3, 7));
	DamageCache continuation(&root);
	EXPECT_LT(continuation.getDamage(actor.get(), target.get(), child), enhanced);
	EXPECT_EQ(continuation.getDamage(actor.get(), target.get(), child),
		child->battleExpectedLuckDamage(BattleAttackInfo(actor.get(), target.get(), 0, false)));
	EXPECT_TRUE(root.matchesSnapshot(snapshot));
	EXPECT_EQ(neighbor->getPosition(), BattleHex(9, 5));
}

TEST_F(NewHorizonsSylvanHybridAITest, DisruptivePreviewAndSelectedReplayPreserveLiveAndSiblingHistories)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:angel", "fortunateAim", 2));
	auto actor = child->getForUpdate(source->unitId());
	auto target = child->getForUpdate(victim->unitId());
	child->moveUnit(neighbor->unitId(), BattleHex(3, 7));
	child->moveUnit(actor->unitId(), BattleHex(9, 5));
	const auto liveState = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	const auto before = child->getSylvanLuckState(BattleSide::ATTACKER);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), child);
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(actor.get(), target.get(), 0, false),
		actor->getPosition(), cache, child);
	ASSERT_TRUE(preview.effectPreview);
	EXPECT_TRUE(preview.effectPreview->getSylvanLuckState(BattleSide::ATTACKER).tactics.disrupted.contains(target->unitId()));
	EXPECT_EQ(child->getSylvanLuckState(BattleSide::ATTACKER), before);
	BattleExchangeVariant exchange;
	exchange.trackAttack(preview, child, cache);
	EXPECT_EQ(child->battleSylvanTacticsInitiativePercent(target.get()), -15);
	EXPECT_EQ(sibling->getSylvanLuckState(BattleSide::ATTACKER), before);
	EXPECT_EQ(battle()->getSylvanLuckState(BattleSide::ATTACKER), liveState);
	child->nextTurn(target->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(child->battleSylvanTacticsInitiativePercent(target.get()), 0);
}

TEST_F(NewHorizonsSylvanHybridAITest, RetaliationDisruptsButSpellLikeAndCollateralDamageDoNot)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:angel", "fortunateAim", 2));
	auto actor = child->getForUpdate(source->unitId());
	auto target = child->getForUpdate(victim->unitId());
	const auto before = child->getSylvanLuckState(BattleSide::ATTACKER);
	for(int context = 0; context < 3; ++context)
	{
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), child);
		auto branchActor = branch->getForUpdate(actor->unitId());
		auto branchTarget = branch->getForUpdate(target->unitId());
		BattleAttackInfo hit(branchActor.get(), branchTarget.get(), 0, false);
		hit.retaliation = context == 0;
		hit.secondaryAttack = context == 1;
		hit.physicalDamage = context != 2;
		sylvanTacticsAI::StrikeProjection projection(branch.get());
		int64_t amount = 10;
		projection.damage(*branch, hit, *branchTarget, amount, context == 2
			? battle::DamageProvenance::SPELL : battle::DamageProvenance::PHYSICAL_CREATURE);
		projection.finish(branchActor->unitId());
		EXPECT_EQ(branch->getSylvanLuckState(BattleSide::ATTACKER).tactics.disrupted.contains(target->unitId()), context == 0);
	}
	EXPECT_EQ(child->getSylvanLuckState(BattleSide::ATTACKER), before);
}

class NewHorizonsSylvanRevisedAITest : public NewHorizonsSylvanAllianceAITest
{
protected:
	int policy() const override { return 4; }
};

TEST_F(NewHorizonsSylvanRevisedAITest, HuntingPackPremiumFollowsTheProjectedTargetsCompletedAction)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:dwarf", "elvenPrecision"));
	auto actor = child->getForUpdate(source->unitId());
	auto target = child->getForUpdate(victim->unitId());
	ASSERT_TRUE(sylvanTacticsAI::allianceDamage(*child, actor.get(), target.get()));
	EXPECT_TRUE(sylvanTacticsAI::dynamicDamage(*child, actor.get(), target.get()));
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*child));
	const BattleAttackInfo hit(actor.get(), target.get(), 0, false);
	DamageCache root;
	root.cacheDamage(actor.get(), target.get(), child);
	const auto premium = root.getDamage(actor.get(), target.get(), child);
	target->completedActionThisRound = true;
	EXPECT_EQ(newHorizonsSylvanTacticsV2::huntingPackBonus(*child, actor.get(), target.get()), 0);
	DamageCache continuation(&root);
	EXPECT_LT(continuation.getDamage(actor.get(), target.get(), child), premium);
	EXPECT_EQ(continuation.getDamage(actor.get(), target.get(), child), child->battleExpectedLuckDamage(hit));
	EXPECT_FALSE(victim->hasCompletedActionThisRound());
}

TEST_F(NewHorizonsSylvanRevisedAITest, ProjectedVoluntaryMovementEarnsDappledCoverOnlyFromTheThirdHex)
{
	ASSERT_NO_FATAL_FAILURE(prepareAlliance("core:centaur", "luckyRecovery"));
	auto * liveShooter = addStack(BattleSide::DEFENDER, creatureByName("core:woodElf"), BattleHex(10, 7), 10);
	resetChild();
	auto actor = child->getForUpdate(source->unitId());
	auto shooter = child->getForUpdate(liveShooter->unitId());
	const BattleAttackInfo shot(shooter.get(), actor.get(), 0, true);
	ASSERT_TRUE(sylvanTacticsAI::dynamicDamage(*child, shooter.get(), actor.get()));
	const auto ordinary = child->battleExpectedLuckDamage(shot);
	gnollHarriersAI::record(*child, actor->unitId(), BattleHex(3, 5), BattleHex(5, 5));
	EXPECT_EQ(newHorizonsSylvanTacticsRevision::dappledCoverPercent(actor.get()), 0);
	EXPECT_EQ(child->battleExpectedLuckDamage(shot), ordinary);
	gnollHarriersAI::record(*child, actor->unitId(), BattleHex(5, 5), BattleHex(6, 5));
	EXPECT_EQ(newHorizonsSylvanTacticsRevision::dappledCoverPercent(actor.get()), 20);
	EXPECT_LT(child->battleExpectedLuckDamage(shot), ordinary);
	EXPECT_EQ(newHorizonsSylvanTacticsRevision::dappledCoverPercent(source), 0);
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(newHorizonsSylvanTacticsRevision::dappledCoverPercent(actor.get()), 0);
	EXPECT_EQ(child->battleExpectedLuckDamage(shot), ordinary);
}
