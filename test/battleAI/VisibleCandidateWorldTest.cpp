/*
 * VisibleCandidateWorldTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequenceFixture.h"
#include "../../AI/BattleAI/VisibleCandidateWorld.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/battle/VisibleHeroPolicy.h"
#include "../mock/mock_battle_IBattleState.h"
#include <bit>
#include <stdexcept>

TEST(VisibleHeroPolicyTest, HiddenUnknownIsNotKnownAbsenceAndNeverReadsTheEnemyHero)
{
	::testing::NiceMock<BattleStateMock> world;
	ON_CALL(world, getSidePlayer(BattleSide::ATTACKER)).WillByDefault(::testing::Return(PlayerColor(0)));
	ON_CALL(world, getSidePlayer(BattleSide::DEFENDER)).WillByDefault(::testing::Return(PlayerColor(1)));
	EXPECT_CALL(world, getSideHero(BattleSide::DEFENDER)).Times(0);
	EXPECT_CALL(world, getSideHero(BattleSide::ATTACKER)).WillOnce(::testing::Return(nullptr));
	CPlayerBattleCallback player(&world, PlayerColor(0)), absent(nullptr, PlayerColor(0));
	const auto hidden = player.battleGetVisibleHeroPolicy(BattleSide::DEFENDER);
	EXPECT_EQ(hidden.kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_FALSE(hidden.matchesSuppliedHero(nullptr));
	EXPECT_EQ(hidden.legacyHero(), nullptr); // Legacy absence branch, NOT knowledge.
	const auto own = player.battleGetVisibleHeroPolicy(BattleSide::ATTACKER);
	EXPECT_EQ(own.kind(), VisibleHeroPolicy::Knowledge::KNOWN_ABSENT);
	EXPECT_TRUE(own.matchesSuppliedHero(nullptr));
	EXPECT_EQ(absent.battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::UNAVAILABLE);
	EXPECT_EQ(player.battleGetVisibleHeroPolicy(BattleSide::INVALID).kind(), VisibleHeroPolicy::Knowledge::UNAVAILABLE);
}

class VisibleCandidateWorldTest : public OriginalMeleeSequenceTest
{
protected:
	void preparePlayer()
	{
		ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
		releaseKnown();
		auto actual = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
		known = std::make_shared<HypotheticBattle>(environment.get(), actual);
		ASSERT_EQ(known->battleGetMySide(), BattleSide::DEFENDER);
		ASSERT_FALSE(known->initialExportSubject());
	}
	AttackPossibility direct(const AttackPossibility & candidate, DamageCache & cache)
	{
		const auto * actor = known->battleGetUnitByID(candidate.attack.attacker->unitId());
		const auto * target = known->battleGetUnitByID(candidate.attack.defender->unitId());
		const auto reachable = known->getReachability(actor);
		const int distance = candidate.from.isValid()
			? std::max(0, SpellTargetEvaluator::physicalTravelDistance(reachable, candidate.from)) : 0;
		BattleAttackInfo attack(actor, target, distance, candidate.attack.shooting);
		return AttackPossibility::evaluate(attack, candidate.from, cache, known);
	}
	void samePreview(const AttackPossibility & actual, const AttackPossibility & legacy)
	{
		EXPECT_EQ(actual.from, legacy.from);
		EXPECT_EQ(actual.dest, legacy.dest);
		const std::array<float, 7> a{actual.defenderDamageReduce, actual.attackerDamageReduce,
			actual.collateralDamageReduce, actual.frozenControlValue, actual.damageDiff(), actual.attackValue(), static_cast<float>(actual.shootersBlockedDmg)};
		const std::array<float, 7> b{legacy.defenderDamageReduce, legacy.attackerDamageReduce,
			legacy.collateralDamageReduce, legacy.frozenControlValue, legacy.damageDiff(), legacy.attackValue(), static_cast<float>(legacy.shootersBlockedDmg)};
		for(size_t i = 0; i < a.size(); ++i)
			EXPECT_EQ(std::bit_cast<uint32_t>(a[i]), std::bit_cast<uint32_t>(b[i]));
		ASSERT_EQ(bool(actual.attackerState), bool(legacy.attackerState));
		if(actual.attackerState)
			EXPECT_EQ(actual.attackerState->save(), legacy.attackerState->save());
		ASSERT_EQ(actual.affectedUnits.size(), legacy.affectedUnits.size());
		for(size_t i = 0; i < actual.affectedUnits.size(); ++i)
		{
			EXPECT_EQ(actual.affectedUnits[i]->unitId(), legacy.affectedUnits[i]->unitId());
			EXPECT_EQ(actual.affectedUnits[i]->save(), legacy.affectedUnits[i]->save());
		}
		ASSERT_EQ(actual.fortuneStrikes.size(), legacy.fortuneStrikes.size());
		for(size_t i = 0; i < actual.fortuneStrikes.size(); ++i)
		{
			EXPECT_EQ(actual.fortuneStrikes[i].attackerId, legacy.fortuneStrikes[i].attackerId);
			EXPECT_EQ(actual.fortuneStrikes[i].defenderId, legacy.fortuneStrikes[i].defenderId);
			EXPECT_EQ(actual.fortuneStrikes[i].hits, legacy.fortuneStrikes[i].hits);
			EXPECT_EQ(actual.fortuneStrikes[i].resolvedHits, legacy.fortuneStrikes[i].resolvedHits);
			EXPECT_EQ(actual.fortuneStrikes[i].resolvedLuck, legacy.fortuneStrikes[i].resolvedLuck);
		}
	}
};

TEST_F(VisibleCandidateWorldTest, OrdinaryPlayerCandidatesUseTheActualMaskedWorldAndKeepUnknownPolicies)
{
	ASSERT_NO_FATAL_FAILURE(preparePlayer());
	DamageCache cache, legacyCache;
	PotentialTargets targets(known->battleGetUnitByID(incoming->unitId()), cache, known);
	ASSERT_FALSE(targets.possibleAttacks.empty());
	for(const auto & candidate : targets.possibleAttacks)
	{
		ASSERT_TRUE(candidate.queryWorld);
		EXPECT_EQ(candidate.queryWorld->callback().get(), known.get());
		const auto own = candidate.queryWorld->heroPolicy(BattleSide::DEFENDER);
		EXPECT_EQ(own.kind(), VisibleHeroPolicy::Knowledge::VISIBLE_HERO);
		EXPECT_TRUE(own.matchesSuppliedHero(defenderSideHero));
		EXPECT_FALSE(own.matchesSuppliedHero(attackerSideHero));
		const auto enemy = candidate.queryWorld->heroPolicy(BattleSide::ATTACKER);
		EXPECT_EQ(enemy.kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
		EXPECT_FALSE(enemy.matchesSuppliedHero(nullptr));
		ASSERT_NO_FATAL_FAILURE(samePreview(candidate, direct(candidate, legacyCache)));
	}
	// The strict authoritative producer/witness is not weakened by this view.
	const auto strict = heroOrderValue::projectOriginalMeleeSequence(environment.get(), known, action());
	EXPECT_EQ(strict.stop, heroOrderValue::MeleeSequenceStop::HIDDEN_HERO_POLICY);
	EXPECT_FALSE(strict.meleeSequenceComplete());
}

TEST_F(VisibleCandidateWorldTest, ActualCandidateToExchangeReplayKeepsScoresStatesAndNativeRng)
{
	ASSERT_NO_FATAL_FAILURE(preparePlayer());
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	DamageCache cache, legacyCache;
	PotentialTargets targets(known->battleGetUnitByID(incoming->unitId()), cache, known);
	ASSERT_FALSE(targets.possibleAttacks.empty());
	const auto & candidate = targets.bestAction();
	const auto legacy = direct(candidate, legacyCache);
	auto actualBranch = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto legacyBranch = std::make_shared<HypotheticBattle>(environment.get(), known);
	BattleExchangeVariant actual, previous;
	DamageCache actualCache, previousCache;
	EXPECT_EQ(std::bit_cast<uint32_t>(actual.trackAttack(candidate, actualBranch, actualCache)),
		std::bit_cast<uint32_t>(previous.trackAttack(legacy, legacyBranch, previousCache)));
	EXPECT_EQ(actual.getScore().ourDamageReduce, previous.getScore().ourDamageReduce);
	EXPECT_EQ(actual.getScore().enemyDamageReduce, previous.getScore().enemyDamageReduce);
	for(const auto * original : {incoming, braced})
		EXPECT_EQ(actualBranch->battleGetUnitByID(original->unitId())->acquireState()->save(),
			legacyBranch->battleGetUnitByID(original->unitId())->acquireState()->save());
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(VisibleCandidateWorldTest, RecipientOnlyHistoryUsesTheSameOrdinaryGeneratorWithoutInventingAnAura)
{
	ASSERT_NO_FATAL_FAILURE(preparePlayer());
	const int other = known->battleGetUnitByID(braced->unitId())->getAttack(false);
	Bonus local(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::OTHER,
		4, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	known->getForUpdate(incoming->unitId())->addUnitBonus({local});
	DamageCache cache, legacyCache;
	PotentialTargets targets(known->battleGetUnitByID(incoming->unitId()), cache, known);
	ASSERT_FALSE(targets.possibleAttacks.empty());
	for(const auto & candidate : targets.possibleAttacks)
		ASSERT_NO_FATAL_FAILURE(samePreview(candidate, direct(candidate, legacyCache)));
	EXPECT_EQ(known->battleGetUnitByID(braced->unitId())->getAttack(false), other);
	EXPECT_EQ(incoming->getAttack(false) + 4, known->battleGetUnitByID(incoming->unitId())->getAttack(false));
}

TEST_F(VisibleCandidateWorldTest, ReturnedCandidateRetainsItsRealSourceAfterTheGeneratorAndCallerReleaseIt)
{
	ASSERT_NO_FATAL_FAILURE(preparePlayer());
	std::weak_ptr<HypotheticBattle> source = known;
	std::optional<AttackPossibility> retained;
	DamageCache cache;
	{
		PotentialTargets targets(known->battleGetUnitByID(incoming->unitId()), cache, known);
		ASSERT_FALSE(targets.possibleAttacks.empty());
		retained = targets.bestAction();
	}
	releaseKnown();
	ASSERT_FALSE(source.expired());
	ASSERT_TRUE(retained->queryWorld);
	EXPECT_EQ(retained->queryWorld->heroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_EQ(retained->attack.attacker->unitId(), incoming->unitId());
	retained.reset();
	EXPECT_TRUE(source.expired()); // Fixture Environment/library still alive; no fake lease claim.
}

TEST_F(VisibleCandidateWorldTest, NullOrForeignActorCannotBeAttachedToAnUnrelatedQueryOwner)
{
	ASSERT_NO_FATAL_FAILURE(preparePlayer());
	EXPECT_THROW(VisibleCandidateWorld(nullptr), std::invalid_argument);
	auto owner = std::make_shared<const VisibleCandidateWorld>(known);
	DamageCache cache;
	auto unrelated = std::make_shared<HypotheticBattle>(environment.get(), known);
	const auto foreign = unrelated->getForUpdate(incoming->unitId());
	EXPECT_THROW(VisibleCandidateWorld::evaluate(owner, foreign.get(), known->battleGetUnitByID(braced->unitId()),
		3, false, BattleHex(92), cache), std::invalid_argument);
	EXPECT_THROW(VisibleCandidateWorld::evaluate(nullptr, nullptr, nullptr, 0, false, BattleHex(92), cache), std::invalid_argument);
}
