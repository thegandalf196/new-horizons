/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/SylvanTacticsProjection.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsSylvanTacticsV2.h"

class NewHorizonsSylvanAllianceAITest : public ProjectedTargetHitFixture
{
protected:
	CStack * source = nullptr;
	CStack * victim = nullptr;
	CStack * neighbor = nullptr;
	void prepareAlliance(const std::string & creature, const std::string & perk)
	{
		startGame();
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck." + perk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck." + perk));
		ASSERT_TRUE(newHorizonsSylvanTacticsV2::enabled(attackerSideHero));
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
