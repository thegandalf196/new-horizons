/*
 * BattlecraftFutureBoundaryTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattlecraftFutureBoundaryFixture.h"
#include "../../lib/networkPacks/SetStackEffect.h"

class BattlecraftFutureBoundaryTest : public battlecraftTest::FutureBoundaryFixture {};

TEST_F(BattlecraftFutureBoundaryTest, MovedStandaloneUsesCanonicalThreeHexApproach)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true, false, true));
	ASSERT_EQ(SpellTargetEvaluator::physicalTravelDistance(
		simulation()->getReachability(actor), from), 3);
	ASSERT_NE(actor->getPosition(), from);
	ASSERT_TRUE(battle()->battleGetHeroOrderStates(BattleSide::DEFENDER).empty());
	ASSERT_NO_FATAL_FAILURE(compare(true));
}

enum class BattlecraftFallback { NO_PERK, NOT_DEFENDING, SPENT, RANGED, SUBSEQUENT };
class BattlecraftFutureFallbackTest : public battlecraftTest::FutureBoundaryFixture,
	public ::testing::WithParamInterface<BattlecraftFallback> {};

TEST_P(BattlecraftFutureFallbackTest, IneligibleReactionRetainsSingleHitAndNativeParity)
{
	const auto mode = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepare(mode != BattlecraftFallback::NO_PERK,
		mode != BattlecraftFallback::NOT_DEFENDING, mode == BattlecraftFallback::RANGED));
	if(mode == BattlecraftFallback::SPENT || mode == BattlecraftFallback::SUBSEQUENT)
	{
		const auto round = battle()->getRound();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMeleeAttack(actor, target, from, false)));
		ASSERT_EQ(battle()->getRound(), round);
		ASSERT_EQ(target->battlecraftPreemptiveStrikeRound, round);
		ASSERT_GT(actor->getAvailableHealth(), 0);
		ASSERT_GT(target->getAvailableHealth(), 0);
		activate(actor); // Test-only extra activation, never a round reset to hide spending.
	}
	if(!ranged)
		ASSERT_EQ(ready(target), 0);
	else
		ASSERT_EQ(ready(target), 50); // Readiness alone must not admit a ranged action.
	ASSERT_NO_FATAL_FAILURE(compare(false, mode != BattlecraftFallback::SUBSEQUENT));
}

INSTANTIATE_TEST_SUITE_P(BattlecraftFutureBoundaries, BattlecraftFutureFallbackTest,
	::testing::Values(BattlecraftFallback::NO_PERK, BattlecraftFallback::NOT_DEFENDING,
		BattlecraftFallback::SPENT, BattlecraftFallback::RANGED, BattlecraftFallback::SUBSEQUENT));

TEST_F(BattlecraftFutureBoundaryTest, SubsequentHitCannotClaimAnUnspentFullAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(extraAttack());
	ASSERT_EQ(ready(target), 50);
	const auto liveBefore = snapshot(*battle());
	{
		auto parent = simulation();
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto before = snapshot(*parent);
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::DEFENDER);
		BattleExchangeVariant future;
		BattleExchangeAttackResult action;
		future.trackAttack(branch->getForUpdate(actor->unitId()), branch->getForUpdate(target->unitId()),
			false, true, cache, branch, false, false, nullptr, nullptr, nullptr, &action);
		EXPECT_FALSE(action.completedAction);
		EXPECT_EQ(branch->getForUpdate(target->unitId())->battlecraftPreemptiveStrikeRound, -1);
		EXPECT_EQ(branch->getForUpdate(actor->unitId())->getAvailableHealth(), actor->getAvailableHealth());
		EXPECT_LT(branch->getForUpdate(target->unitId())->getAvailableHealth(), target->getAvailableHealth());
		EXPECT_EQ(snapshot(*parent), before);
		EXPECT_EQ(snapshot(*sibling), before);
		EXPECT_EQ(snapshot(*battle()), liveBefore);
	}
	// This is the subsequent-hit overload contract, not a complete native action.
	// Full-action selected/native parity belongs to compare() and the real exchange test.
}

TEST_F(BattlecraftFutureBoundaryTest, FullyAbsorbedPreHitStillConsumesTheAcceptedMarker)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT,
		BonusSource::SPELL_EFFECT, 1000000, BonusSourceID(SpellID(SpellID::HASTE)));
	guardian.turnsRemain = 2;
	SetStackEffect effects;
	effects.battleID = BattleID(0);
	effects.toAdd.emplace_back(actor->unitId(), std::vector<Bonus>{guardian});
	gameHandler->sendAndApply(effects);
	const auto health = actor->getAvailableHealth();
	ASSERT_EQ(actor->guardianSpiritHitPoints, 1000000);
	ASSERT_NO_FATAL_FAILURE(compare(true));
	EXPECT_EQ(actor->getAvailableHealth(), health);
	EXPECT_GT(actor->guardianSpiritHitPoints, 0);
	EXPECT_LT(actor->guardianSpiritHitPoints, 1000000);
	EXPECT_EQ(target->battlecraftPreemptiveStrikeRound, battle()->getRound());
}

TEST_F(BattlecraftFutureBoundaryTest, MultiplePrimaryBlowsAreOneCompletedFutureAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(extraAttack());
	ASSERT_NO_FATAL_FAILURE(compare(true));
	ASSERT_EQ(server.attacks.size(), 3u);
	EXPECT_EQ(server.attacks[0].stackAttacking, target->unitId());
	EXPECT_EQ(server.attacks[1].stackAttacking, actor->unitId());
	EXPECT_EQ(server.attacks[2].stackAttacking, actor->unitId());
}

class BattlecraftFutureProtectTest : public battlecraftTest::FutureBoundaryFixture,
	public ::testing::WithParamInterface<bool> {};

TEST_P(BattlecraftFutureProtectTest, CurrentRecipientAndLaterWardRetainIndependentReadiness)
{
	const bool readyProtector = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepare(true, !readyProtector));
	protector = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 5), 1000);
	forceMaximumDamage(protector);
	blockRetaliation(protector);
	if(readyProtector)
	{
		ASSERT_TRUE(defend(protector));
	}
	ASSERT_NO_FATAL_FAILURE(extraAttack());
	// Issue the Order on the non-Defending recipient's activation. Starting
	// the ready recipient's TURN_QUEUE activation would expire its Defend.
	activate(readyProtector ? target : protector);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makePairedHeroCommand(BattleSide::DEFENDER, HeroCommand::PROTECT,
			protector->unitId(), target->unitId())));
	activate(actor);
	ASSERT_EQ(battle()->battleResolveHeroOrderTarget(actor, target, false), protector);
	ASSERT_EQ(battle()->battleHeroOrderProtectInterceptionLimit(BattleSide::DEFENDER), 1);
	ASSERT_EQ(ready(protector), readyProtector ? 50 : 0);
	ASSERT_EQ(ready(target), readyProtector ? 0 : 50);
	ASSERT_EQ(protector->defended(), readyProtector);
	ASSERT_EQ(target->defended(), !readyProtector);
	ASSERT_NO_FATAL_FAILURE(compare(true));
	ASSERT_EQ(server.attacks.size(), 3u);
	const auto round = battle()->getRound();
	EXPECT_EQ(protector->battlecraftPreemptiveStrikeRound, readyProtector ? round : -1);
	EXPECT_EQ(target->battlecraftPreemptiveStrikeRound, readyProtector ? -1 : round);
	if(readyProtector)
	{
		EXPECT_EQ(server.attacks[0].stackAttacking, protector->unitId());
		EXPECT_EQ(server.attacks[1].bsa.front().stackAttacked, protector->unitId());
	}
	else
	{
		EXPECT_EQ(server.attacks[0].stackAttacking, actor->unitId());
		EXPECT_EQ(server.attacks[0].bsa.front().stackAttacked, protector->unitId());
		EXPECT_EQ(server.attacks[1].stackAttacking, target->unitId());
	}
	EXPECT_EQ(server.attacks[2].stackAttacking, actor->unitId());
	EXPECT_EQ(server.attacks[2].bsa.front().stackAttacked, target->unitId());
}

INSTANTIATE_TEST_SUITE_P(BattlecraftFutureRecipients, BattlecraftFutureProtectTest, ::testing::Bool());
