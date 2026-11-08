/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/VisibleCandidateWorld.h"
#include "../../AI/BattleAI/PhysicalStrikeProjection.h"
#include "../../lib/battle/NewHorizonsSpecialtyEvents.h"
#include "../../lib/battle/NewHorizonsSpecialtyRoundEffects.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"

using namespace heroQueueTest;

class NativeSpecialtyProjectionTest : public HeroQueueSelectorTest
{
protected:
	NativeSpecialtyProjectionTest() { useRevisedOrders = true; }
	void prepare(const char * hero)
	{
		ASSERT_NO_FATAL_FAILURE(world());
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(hero)));
		ASSERT_NO_FATAL_FAILURE(arena());
	}
	void branches()
	{
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
	AttackPossibility forecast(const battle::Unit * first, const battle::Unit * second, DamageCache & cache)
	{
		return AttackPossibility::evaluate(BattleAttackInfo(child->battleGetUnitByID(first->unitId()),
			child->battleGetUnitByID(second->unitId()), 0, false), first->getPosition(), cache, child);
	}
};

TEST_F(NativeSpecialtyProjectionTest, AcceptedGoblinStrikeSpendsOnceAndInvalidatesWarmDamageCache)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gretchin"));
	const auto * goblin = addStack(BattleSide::ATTACKER, creatureByName("core:goblin"), BattleHex(6, 3), 1000);
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 3), 1000);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	DamageCache cache;
	cache.cacheDamage(goblin, target, child);
	const auto preview = forecast(goblin, target, cache);
	ASSERT_NE(preview.effectPreview, nullptr);
	const auto * spent = preview.effectPreview->battleGetUnitByID(goblin->unitId());
	ASSERT_TRUE(spent->hasBonus(CSelector(newHorizonsSpecialtyEvents::isGoblinFrenzySpent)));
	BattleAttackInfo next(spent, preview.effectPreview->battleGetUnitByID(target->unitId()), 0, false);
	EXPECT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*preview.effectPreview, next), 100);
	DamageCache fresh;
	EXPECT_EQ(cache.getDamage(next.attacker, next.defender, preview.effectPreview),
		fresh.getDamage(next.attacker, next.defender, preview.effectPreview));
	EXPECT_FALSE(parent->battleGetUnitByID(goblin->unitId())->hasBonus(CSelector(newHorizonsSpecialtyEvents::isGoblinFrenzySpent)));
	EXPECT_FALSE(sibling->battleGetUnitByID(goblin->unitId())->hasBonus(CSelector(newHorizonsSpecialtyEvents::isGoblinFrenzySpent)));
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeSpecialtyProjectionTest, LethalAttackStillPublishesLacusDefenseReceiptWithoutMutatingLive)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:lacus"));
	const auto * water = addStack(BattleSide::ATTACKER, creatureByName("core:waterElemental"), BattleHex(6, 3), 1);
	const auto * hitter = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 3), 10000);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	DamageCache cache;
	const auto preview = forecast(hitter, water, cache);
	ASSERT_NE(preview.effectPreview, nullptr);
	const auto * fallen = preview.effectPreview->battleGetUnitByID(water->unitId());
	ASSERT_FALSE(fallen->alive());
	const auto receipt = fallen->getBonuses(CSelector(newHorizonsSpecialtyEvents::isUndertowDefense));
	ASSERT_EQ(receipt->size(), 1);
	EXPECT_EQ(receipt->front()->val, 1);
	EXPECT_FALSE(parent->battleGetUnitByID(water->unitId())->hasBonus(CSelector(newHorizonsSpecialtyEvents::isUndertowDefense)));
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeSpecialtyProjectionTest, KaltRoundAppendsSameStackOncePerBoundaryInIsolatedChild)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:kalt"));
	const auto * water = addStack(BattleSide::ATTACKER, creatureByName("core:waterElemental"), BattleHex(6, 3), 10);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto size = child->battleGetAllUnits(false).size();
	child->nextRound();
	const auto grown = child->getForUpdate(water->unitId());
	EXPECT_EQ(grown->getCount(), 11);
	EXPECT_FALSE(newHorizonsSpecialtyEvents::applyRound(*child, *grown, child->battleGetRound()));
	EXPECT_EQ(grown->getCount(), 11);
	EXPECT_EQ(child->battleGetAllUnits(false).size(), size);
	EXPECT_EQ(parent->battleGetUnitByID(water->unitId())->getCount(), 10);
	EXPECT_EQ(sibling->battleGetUnitByID(water->unitId())->getCount(), 10);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeSpecialtyProjectionTest, StrakerRoundRestoresAtMostOneHundredHPWithoutExceedingStartingCount)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:straker"));
	auto * zombie = addStack(BattleSide::ATTACKER, creatureByName("core:walkingDead"), BattleHex(6, 3), 10);
	ASSERT_NE(zombie, nullptr);
	injure(zombie, 2 * zombie->getMaxHealth() - 1);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto before = zombie->getAvailableHealth();
	child->nextRound();
	const auto restored = child->getForUpdate(zombie->unitId());
	EXPECT_EQ(restored->getAvailableHealth(), std::min<int64_t>(10 * zombie->getMaxHealth(), before + 100));
	EXPECT_LE(restored->getCount(), 10);
	EXPECT_EQ(parent->battleGetUnitByID(zombie->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(sibling->battleGetUnitByID(zombie->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeSpecialtyProjectionTest, ValidatedKorbacApproachCarriesTravelOnlyIntoMatchingRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:korbac"));
	const auto * flies = addStack(BattleSide::ATTACKER, creatureByName("core:serpentFly"), BattleHex(6, 3), 1000);
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 3), 1000);
	ASSERT_NE(flies, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto * current = child->battleGetUnitByID(flies->unitId());
	const BattleHex approach(9, 3);
	const auto reachable = child->getReachability(current);
	const int distance = SpellTargetEvaluator::physicalTravelDistance(reachable, approach);
	ASSERT_GT(distance, 0);
	auto world = std::make_shared<const VisibleCandidateWorld>(child);
	DamageCache cache;
	const auto preview = VisibleCandidateWorld::evaluate(world, current,
		child->battleGetUnitByID(target->unitId()), distance, false, approach, cache, distance, true);
	EXPECT_EQ(preview.attack.voluntaryTravelDistance, distance);
	const auto opening = std::ranges::find_if(preview.fortuneStrikes, [&](const auto & hit)
		{ return hit.attackerId == flies->unitId() && !hit.retaliation && hit.attackIndex == 0; });
	const auto response = std::ranges::find_if(preview.fortuneStrikes, [&](const auto & hit)
		{ return hit.attackerId == target->unitId() && hit.retaliation; });
	ASSERT_NE(opening, preview.fortuneStrikes.end());
	ASSERT_NE(response, preview.fortuneStrikes.end());
	auto survivingTarget = dynamic_cast<const battle::CUnitState *>(target)->acquireState();
	for(const auto & [id, damage] : opening->resolvedHits)
		if(id == target->unitId()) { auto applied = damage; survivingTarget->damage(applied); }
	BattleAttackInfo expectedCounter(survivingTarget.get(), current, 0, false);
	expectedCounter.retaliation = true;
	expectedCounter.attackerPos = target->getPosition();
	expectedCounter.defenderPos = approach;
	expectedCounter.defenderVoluntaryTravelDistance = distance;
	const auto counterHit = std::ranges::find(response->hits, flies->unitId(), &std::pair<uint32_t, int64_t>::first);
	ASSERT_NE(counterHit, response->hits.end());
	EXPECT_EQ(counterHit->second, child->battleExpectedLuckDamage(expectedCounter));
	const auto reverse = preview.attack.reverse();
	EXPECT_EQ(reverse.defenderVoluntaryTravelDistance, distance);
	EXPECT_EQ(newHorizonsSpecialtyEvents::retaliationReduction(*child, reverse), std::min(100, 3 * distance));
	auto unrelated = reverse;
	unrelated.defenderVoluntaryTravelDistance = 0;
	EXPECT_EQ(newHorizonsSpecialtyEvents::retaliationReduction(*child, unrelated), 0);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeSpecialtyProjectionTest, AcceptedZeroDamageConsumesGoblinFirstStrikeReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gretchin"));
	const auto * goblin = addStack(BattleSide::ATTACKER, creatureByName("core:goblin"), BattleHex(6, 3), 100);
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 3), 100);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto projectedGoblin = child->getForUpdate(goblin->unitId());
	const auto projectedTarget = child->getForUpdate(target->unitId());
	BattleAttackInfo accepted(projectedGoblin.get(), projectedTarget.get(), 0, false);
	ASSERT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*child, accepted), 150);
	PhysicalStrikeProjection projection(child.get());
	int64_t amount = 0;
	projection.damage(*child, accepted, *projectedTarget, amount, battle::DamageProvenance::PHYSICAL_CREATURE);
	projection.finish(projectedGoblin->unitId());
	EXPECT_EQ(projectedTarget->getAvailableHealth(), target->getAvailableHealth());
	EXPECT_TRUE(projectedGoblin->hasBonus(CSelector(newHorizonsSpecialtyEvents::isGoblinFrenzySpent)));
	EXPECT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*child, accepted), 100);
	EXPECT_EQ(observation(), outside);
}
