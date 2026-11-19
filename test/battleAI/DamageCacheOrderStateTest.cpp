/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/DamageCacheAdmission.h"
#include "../../lib/battle/ConsecratedArms.h"
#include "../../lib/battle/BattleInfo.h"

class DamageCacheOrderStateTest : public ProjectedTargetHitFixture
{
protected:
	void mark(int premium = 0)
	{
		HeroOrderState order;
		order.command = HeroCommand::FOCUS_FIRE;
		order.issuedRound = child->battleGetRound();
		order.primaryTargetUnitId = incoming->unitId();
		child->setHeroOrderState(BattleSide::ATTACKER, order);
		child->setFocusFireState(BattleSide::ATTACKER,
			FocusFireState{incoming->unitId(), {braced->unitId()}, child->battleGetRound(), premium});
	}
	int64_t direct() const
	{
		const auto * actor = child->battleGetUnitByID(braced->unitId());
		const auto * victim = child->battleGetUnitByID(incoming->unitId());
		return child->battleExpectedLuckDamage(BattleAttackInfo(actor, victim, 0,
			child->battleCanShoot(actor, victim->getPosition())));
	}
};

TEST_F(DamageCacheOrderStateTest, CanonicalRosterWithoutOrdersCachesCurrentDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(battle()->heroOrderDamageStatePresence(), std::optional<bool>(false));
	ASSERT_EQ(child->heroOrderDamageStatePresence(), std::optional<bool>(false));
	const auto actor = child->getForUpdate(braced->unitId());
	const auto victim = child->getForUpdate(incoming->unitId());
	DamageCache cache;
	cache.buildDamageCache(child, BattleSide::ATTACKER);
	DamageCacheProbeTiming timing;
	EXPECT_EQ(cache.getDamage(actor.get(), victim.get(), child, &timing), direct());
	EXPECT_EQ(timing.dynamicBypasses, 0u);
	EXPECT_EQ(timing.rootCacheHits, 1u);
	EXPECT_EQ(timing.cacheFillCalls, 0u);
}

TEST_F(DamageCacheOrderStateTest, NativeAndProjectedOrderPresenceFollowTheirOwnState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(braced->unitId());
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(battle()->heroOrderDamageStatePresence(), std::optional<bool>(true));
	EXPECT_EQ(child->heroOrderDamageStatePresence(), std::optional<bool>(false));
	resetChild();
	EXPECT_EQ(child->heroOrderDamageStatePresence(), std::optional<bool>(true));
	child->nextRound();
	EXPECT_EQ(child->heroOrderDamageStatePresence(), std::optional<bool>(false));
	EXPECT_EQ(battle()->heroOrderDamageStatePresence(), std::optional<bool>(true));
}

TEST_F(DamageCacheOrderStateTest, ActiveCaptureCannotReviveAfterExpiryAndParentOriginalStaysCaptured)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(braced->unitId());
	const auto victim = child->getForUpdate(incoming->unitId());
	actor->addUnitBonus(std::vector<Bonus>{
		Bonus(BonusDuration::ONE_BATTLE, BonusType::SHOOTER, BonusSource::OTHER, 1, BonusSourceID()),
		Bonus(BonusDuration::ONE_BATTLE, BonusType::SHOTS, BonusSource::OTHER, 10, BonusSourceID())});
	const auto ordinary = direct();
	mark(40);
	ASSERT_TRUE(child->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	ASSERT_EQ(child->battleTargetedRangedCommandPercent(actor.get(), victim.get(), true), 40);
	const auto premium = direct();
	ASSERT_GT(premium, ordinary);
	DamageCache parent;
	parent.cacheDamage(actor.get(), victim.get(), child);
	const auto parentBefore = parent.captureSnapshot();
	DamageCache cache(&parent);
	cache.cacheDamage(actor.get(), victim.get(), child);
	DamageCacheProbeTiming active;
	EXPECT_EQ(cache.getDamage(actor.get(), victim.get(), child, &active), premium);
	EXPECT_EQ(active.dynamicBypasses, 1u);
	child->nextRound();
	ASSERT_FALSE(child->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	ASSERT_FALSE(child->getFocusFireState(BattleSide::ATTACKER));
	ASSERT_EQ(child->heroOrderDamageStatePresence(), std::optional<bool>(false));
	DamageCacheProbeTiming expired;
	EXPECT_EQ(cache.getDamage(actor.get(), victim.get(), child, &expired), ordinary);
	EXPECT_EQ(expired.dynamicBypasses, 0u);
	EXPECT_EQ(expired.cacheFillCalls, 1u);
	EXPECT_EQ(cache.getOriginalDamage(actor.get(), victim.get(), child), premium);
	EXPECT_TRUE(parent.matchesSnapshot(parentBefore));
	DamageCacheProbeTiming repeat;
	EXPECT_EQ(cache.getDamage(actor.get(), victim.get(), child, &repeat), ordinary);
	EXPECT_EQ(repeat.localChildCacheHits, 1u);
	EXPECT_EQ(repeat.cacheFillCalls, 0u);
	EXPECT_EQ(parent.getDamage(actor.get(), victim.get(), child), ordinary);
	EXPECT_FALSE(parent.matchesSnapshot(parentBefore));
}

TEST_F(DamageCacheOrderStateTest, MarkerOnlyOrderAndPrayerProtectionRemainDynamicAfterRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(braced->unitId());
	const auto victim = child->getForUpdate(incoming->unitId());
	consecratedArms::Policy policy;
	policy.rank = 1;
	const auto prayer = consecratedArms::prayerBonuses(consecratedArms::Prayer::DIVINE_PURPOSE, 75, policy, 2, 1);
	Bonus command(BonusDuration::N_TURNS, BonusType::LUCK,
		BonusSource::HERO_COMMAND, 1, BonusSourceID());
	command.turnsRemain = 2;
	const std::vector<Bonus> order{command};
	for(const auto & effects : {order, prayer})
	{
		DamageCache cache;
		cache.buildDamageCache(child, BattleSide::ATTACKER);
		child->addUnitBonus(actor->unitId(), effects);
		ASSERT_EQ(child->heroOrderDamageStatePresence(), std::optional<bool>(false));
		DamageCacheProbeTiming active;
		EXPECT_EQ(cache.getDamage(actor.get(), victim.get(), child, &active), direct());
		EXPECT_EQ(active.dynamicBypasses, 1u);
		child->removeUnitBonus(actor->unitId(), effects);
		DamageCacheProbeTiming removed;
		EXPECT_EQ(cache.getDamage(actor.get(), victim.get(), child, &removed), direct());
		EXPECT_EQ(removed.dynamicBypasses, 1u);
	}
}

TEST_F(DamageCacheOrderStateTest, SnapshotDistinguishesZeroPremiumOrderCaptureFromOrdinaryCapture)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(braced->unitId());
	const auto victim = child->getForUpdate(incoming->unitId());
	mark();
	DamageCache cache;
	cache.cacheDamage(actor.get(), victim.get(), child);
	const auto captured = cache.captureSnapshot();
	const auto before = direct();
	child->nextRound();
	ASSERT_FALSE(child->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	ASSERT_FALSE(child->getFocusFireState(BattleSide::ATTACKER));
	ASSERT_EQ(direct(), before);
	cache.cacheDamage(actor.get(), victim.get(), child);
	EXPECT_FALSE(cache.matchesSnapshot(captured));
}

TEST_F(DamageCacheOrderStateTest, TimedSpellCaptureAndExpiryKeepCurrentDamageAndParentOriginalSeparate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(braced->unitId());
	const auto victim = child->getForUpdate(incoming->unitId());
	const auto ordinary = direct();
	Bonus bloodlust(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT,
		10, BonusSourceID(SpellID(SpellID::BLOODLUST)), BonusSubtypeID(PrimarySkill::ATTACK));
	bloodlust.turnsRemain = 1;
	child->addUnitBonus(actor->unitId(), {bloodlust});
	const auto boosted = direct();
	ASSERT_GT(boosted, ordinary);
	DamageCache parent;
	parent.cacheDamage(actor.get(), victim.get(), child);
	const auto saved = parent.captureSnapshot();
	DamageCache cache(&parent);
	cache.cacheDamage(actor.get(), victim.get(), child);
	child->nextRound();
	ASSERT_EQ(direct(), ordinary);
	DamageCacheProbeTiming expired;
	EXPECT_EQ(cache.getDamage(actor.get(), victim.get(), child, &expired), ordinary);
	EXPECT_EQ(expired.dynamicBypasses, 1u);
	EXPECT_EQ(cache.getOriginalDamage(actor.get(), victim.get(), child), boosted);
	EXPECT_TRUE(parent.matchesSnapshot(saved));
}
