/*
 * ProtectAttackPreviewTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/HeroActionContinuation.h"

#include <array>
#include <cmath>

class ProtectAttackPreviewTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * ward = nullptr;
	CStack * protector = nullptr;
	std::shared_ptr<projectedHitTest::Environment> environment;

	void activate(const CStack * unit)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = unit->unitId();
		pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
	}

	void prepare(bool splash, bool academyPair = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(89), 100);
		ward = addStack(BattleSide::ATTACKER,
			creatureByName(academyPair ? "core:masterGremlin" : "core:pikeman"), BattleHex(93), 100);
		// HOTRAITS retains the reversed internal Golem IDs: ironGolem is Stone Golem.
		protector = addStack(BattleSide::ATTACKER,
			creatureByName(academyPair ? "core:ironGolem" : "core:pikeman"), BattleHex(94), 100);
		if(academyPair)
		{
			ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
			ASSERT_EQ(ward->unitType()->getJsonKey(), "core:masterGremlin");
			ASSERT_EQ(protector->unitType()->getJsonKey(), "core:ironGolem");
			ASSERT_TRUE(ward->isShooter());
		}
		for(auto * unit : {actor, ward, protector})
		{
			ASSERT_FALSE(unit->doubleWide());
			forceMaximumDamage(unit);
		}
		if(splash)
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::ATTACKS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
		// The remote counter still has a primary, but its adjacent Ward must
		// not become counter collateral merely because the Protector has sweep.
		if(!academyPair)
			protector->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::ATTACKS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
		activate(protector);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
				protector->unitId(), ward->unitId())));
		activate(actor);
		ASSERT_TRUE(battle()->isMeleeAttackPossible(actor, ward, BattleHex(92)));
		ASSERT_FALSE(battle()->isMeleeAttackPossible(actor, protector, BattleHex(92)));
		ASSERT_EQ(battle()->battleResolveHeroOrderTarget(actor, ward, false), protector);
		environment = std::make_shared<projectedHitTest::Environment>(gameState());
	}

	std::string branchState(const CBattleInfoCallback & model) const
	{
		std::string result;
		for(const auto * unit : {actor, ward, protector})
			result += model.battleGetUnitByID(unit->unitId())->acquireState()->save().toString();
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto orders = projectedHitTest::serialized(model.getBattle()->getHeroOrderStates(side));
			const auto ledger = projectedHitTest::serialized(model.getBattle()->getHeroActionAllowances(side));
			result.append(reinterpret_cast<const char *>(orders.data()), orders.size());
			result.append(reinterpret_cast<const char *>(ledger.data()), ledger.size());
		}
		return result;
	}

	std::array<size_t, 3> packetCounts() const
	{
		return {server.attacks.size(), server.startedActions.size(), server.orderStateUpdates.size()};
	}

	std::vector<std::byte> nativeState() const
	{
		auto bytes = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto append = [&bytes](const auto & value)
		{
			const auto more = projectedHitTest::serialized(value);
			bytes.insert(bytes.end(), more.begin(), more.end());
		};
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			append(battle()->getHeroOrderStates(side));
			append(battle()->getHeroActionAllowances(side));
		}
		return bytes;
	}

	void compareNative(bool splash, bool academyPair = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(splash, academyPair));
		const auto untouched = branchState(*battle());
		const auto rngOrdersLedger = nativeState();
		const auto traffic = packetCounts();
		std::array<int64_t, 3> health{};
		int counters = -1;
		std::vector<FortuneStrikeProjection> receipts;
		{
			auto player = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
			auto parent = std::make_shared<HypotheticBattle>(environment.get(), player);
			auto candidate = std::make_shared<HypotheticBattle>(environment.get(), parent);
			auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
			const auto parentBefore = branchState(*parent);
			const auto siblingBefore = branchState(*sibling);
			DamageCache cache;
			cache.buildDamageCache(candidate, BattleSide::DEFENDER);
			const auto preview = AttackPossibility::evaluate(
				BattleAttackInfo(candidate->battleGetUnitByID(actor->unitId()),
					candidate->battleGetUnitByID(ward->unitId()), 3, false), BattleHex(92), cache, candidate);
			ASSERT_TRUE(preview.effectPreview);
			ASSERT_TRUE(preview.attackerState);
			health = {preview.attackerState->getAvailableHealth(),
				preview.effectPreview->battleGetUnitByID(ward->unitId())->getAvailableHealth(),
				preview.effectPreview->battleGetUnitByID(protector->unitId())->getAvailableHealth()};
			counters = preview.effectPreview->battleGetUnitByID(protector->unitId())->acquireState()->counterAttacks.available();
			receipts = preview.fortuneStrikes;
			ASSERT_EQ(receipts.size(), 2u);
			EXPECT_EQ(receipts.front().defenderId, protector->unitId());
			EXPECT_TRUE(receipts.back().retaliation);
			EXPECT_EQ(receipts.back().attackerId, protector->unitId());
			ASSERT_EQ(receipts.back().resolvedHits.size(), 1u);
			EXPECT_EQ(receipts.back().resolvedHits.front().first, actor->unitId());
			EXPECT_EQ(branchState(*parent), parentBefore);
			EXPECT_EQ(branchState(*sibling), siblingBefore);
			EXPECT_EQ(branchState(*candidate), untouched);
		}
		EXPECT_EQ(branchState(*battle()), untouched);
		EXPECT_EQ(nativeState(), rngOrdersLedger);
		EXPECT_EQ(packetCounts(), traffic);
		const auto wardBefore = ward->getAvailableHealth();
		server.attacks.clear();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeMeleeAttack(actor, ward, BattleHex(92), false)));
		ASSERT_EQ(server.attacks.size(), 2u) << "Actual remote counter is the native oracle";
		EXPECT_TRUE(server.attacks.back().counter());
		EXPECT_EQ(server.attacks.back().stackAttacking, protector->unitId());
		EXPECT_EQ(actor->getAvailableHealth(), health[0]);
		EXPECT_EQ(ward->getAvailableHealth(), health[1]);
		EXPECT_EQ(protector->getAvailableHealth(), health[2]);
		EXPECT_EQ(protector->acquireState()->counterAttacks.available(), counters);
		if(splash)
			EXPECT_LT(ward->getAvailableHealth(), wardBefore);
		else
			EXPECT_EQ(ward->getAvailableHealth(), wardBefore);
		for(size_t index = 0; index < receipts.size(); ++index)
		{
			const auto & actual = server.attacks[index];
			EXPECT_EQ(actual.stackAttacking, receipts[index].attackerId);
			EXPECT_EQ(actual.counter(), receipts[index].retaliation);
			ASSERT_EQ(actual.bsa.size(), receipts[index].resolvedHits.size());
			for(size_t hit = 0; hit < actual.bsa.size(); ++hit)
			{
				EXPECT_EQ(actual.bsa[hit].stackAttacked, receipts[index].resolvedHits[hit].first);
				EXPECT_EQ(actual.bsa[hit].damageAmount, receipts[index].resolvedHits[hit].second);
			}
		}
	}

	void compareContinuation()
	{
		auto player = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
		auto parent = std::make_shared<HypotheticBattle>(environment.get(), player);
		auto candidate = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto before = branchState(*battle());
		const auto rngOrdersLedger = nativeState();
		const auto traffic = packetCounts();
		DamageCache cache;
		cache.buildDamageCache(candidate, BattleSide::DEFENDER);
		BattleExchangeEvaluator evaluator(candidate, environment, 1.0f, 2);
		heroActionUtility::AbsoluteContinuationUtility utility{0};
		ASSERT_NO_THROW(heroActionUtility::evaluateContinuation(utility,
			candidate->getForUpdate(actor->unitId()), true, CachedAttack{}, cache,
			candidate, environment, 1.0f, 2, evaluator, nullptr));
		EXPECT_TRUE(std::isfinite(utility.value));
		EXPECT_EQ(branchState(*parent), before);
		EXPECT_EQ(branchState(*sibling), before);
		EXPECT_EQ(branchState(*battle()), before);
		EXPECT_EQ(nativeState(), rngOrdersLedger);
		EXPECT_EQ(packetCounts(), traffic);
	}
};

TEST_F(ProtectAttackPreviewTest, NativeRemoteCounterRetainsOnlyItsPrimaryVictim)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto wardHealth = ward->getAvailableHealth();
	const auto actorHealth = actor->getAvailableHealth();
	const auto counters = protector->acquireState()->counterAttacks.available();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(actor, ward, BattleHex(92), false)));
	ASSERT_EQ(server.attacks.size(), 2u);
	const auto & counter = server.attacks.back();
	EXPECT_TRUE(counter.counter());
	EXPECT_EQ(counter.stackAttacking, protector->unitId());
	ASSERT_EQ(counter.bsa.size(), 1u);
	EXPECT_EQ(counter.bsa.front().stackAttacked, actor->unitId());
	EXPECT_GT(counter.bsa.front().damageAmount, 0);
	EXPECT_LT(actor->getAvailableHealth(), actorHealth);
	EXPECT_EQ(ward->getAvailableHealth(), wardHealth);
	EXPECT_EQ(protector->acquireState()->counterAttacks.available(), counters - 1);
}

TEST_F(ProtectAttackPreviewTest, AsymmetricContactMatchesNativeRedirectAndRemotePrimaryCounter)
{
	ASSERT_NO_FATAL_FAILURE(compareNative(false));
}

TEST_F(ProtectAttackPreviewTest, SweepUsesWardContactAndRemoteCounterHasNoCollateral)
{
	ASSERT_NO_FATAL_FAILURE(compareNative(true));
}

TEST_F(ProtectAttackPreviewTest, FullOrderContinuationSearchLeavesNativeParentAndSiblingUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(compareContinuation());
}

TEST_F(ProtectAttackPreviewTest, StoneGolemProtectingMasterCabirMatchesNativeAttackAndRemoteCounter)
{
	ASSERT_NO_FATAL_FAILURE(compareNative(false, true));
}

TEST_F(ProtectAttackPreviewTest, StoneGolemProtectingMasterCabirSupportsFullOrderContinuation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	ASSERT_NO_FATAL_FAILURE(compareContinuation());
}
