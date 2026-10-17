/*
 * BraceAttackPreviewTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequenceFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/PreemptiveAttackRequest.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"

class BraceAttackPreviewTest : public OriginalMeleeSequenceTest
{
protected:

	void prepareControlledBrace(bool controlledActor)
	{
		ASSERT_NO_FATAL_FAILURE(prepareSequence(false));
		releaseKnown();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		remove.changedStacks.emplace_back(braced->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		braced = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(93), 10);
		auto * controlled = controlledActor ? incoming : braced;
		controlled->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED,
			BonusSource::OTHER, 1, BonusSourceID()));
		forceMaximumDamage(braced);
		ASSERT_EQ(braced->unitSide(), BattleSide::DEFENDER);
		ASSERT_EQ(incoming->unitSide(), BattleSide::DEFENDER);
		const auto owner = battle()->battleGetOwner(braced);
		const auto side = battle()->playerToSide(owner);
		activate(braced->unitId());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), owner,
			BattleAction::makeHeroCommand(side, HeroCommand::BRACE)));
		activate(incoming->unitId());
		ASSERT_TRUE(battle()->battleCanTriggerHeroOrderBrace(incoming, braced, 3, false, false));
	}

	void compareRealPreview(bool brace, bool hypnotized = false)
	{
		if(hypnotized)
			ASSERT_NO_FATAL_FAILURE(prepareControlledBrace(true));
		else
		{
			ASSERT_NO_FATAL_FAILURE(prepareSequence(brace));
			releaseKnown();
		}
		const auto controller = battle()->battleGetActionController(incoming);
		const auto side = battle()->playerToSide(controller);
		ASSERT_EQ(controller, hypnotized ? PlayerColor(0) : PlayerColor(1));
		ASSERT_TRUE(battle()->battleCanAttackUnitAction(incoming, braced));
		auto player = std::make_shared<CPlayerBattleCallback>(battle(), controller);
		auto candidate = std::make_shared<HypotheticBattle>(environment.get(), player);
		ASSERT_EQ(candidate->battleGetMySide(), side);
		const auto nativeIncoming = incoming->acquireState()->save();
		const auto nativeBraced = braced->acquireState()->save();
		const auto randomBefore = projectedHitTest::serialized(*gameHandler->randomizer);
		DamageCache cache;
		cache.buildDamageCache(candidate, side);
		std::optional<AttackPossibility> preview = AttackPossibility::evaluate(
			BattleAttackInfo(candidate->battleGetUnitByID(incoming->unitId()),
				candidate->battleGetUnitByID(braced->unitId()), 3, false), BattleHex(92), cache, candidate);
		ASSERT_TRUE(preview->attackerState);
		ASSERT_EQ(preview->affectedUnits.size(), 1);
		const auto actorHealth = preview->attackerState->getAvailableHealth();
		const auto targetHealth = preview->affectedUnits.front()->getAvailableHealth();
		const auto receipts = preview->fortuneStrikes;
		EXPECT_EQ(std::ranges::count(receipts, true, &FortuneStrikeProjection::bracePreemptive), brace ? 1 : 0);
		if(brace)
		{
			ASSERT_TRUE(preview->effectPreview);
			ASSERT_FALSE(receipts.empty());
			EXPECT_TRUE(receipts.front().bracePreemptive);
			EXPECT_FALSE(receipts.front().retaliation);
			EXPECT_EQ(receipts.front().attackerId, braced->unitId());
			EXPECT_EQ(receipts.front().defenderId, incoming->unitId());
		}
		EXPECT_EQ(incoming->acquireState()->save(), nativeIncoming);
		EXPECT_EQ(braced->acquireState()->save(), nativeBraced);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), randomBefore);
		if(hypnotized)
		{
			auto sibling = std::make_shared<HypotheticBattle>(environment.get(), candidate);
			const auto actorBefore = sibling->getForUpdate(incoming->unitId())->getAvailableHealth();
			const auto targetBefore = sibling->getForUpdate(braced->unitId())->getAvailableHealth();
			BattleExchangeVariant exchange;
			exchange.trackAttack(*preview, candidate, cache);
			EXPECT_EQ(candidate->battleGetUnitByID(incoming->unitId())->getAvailableHealth(), actorHealth);
			EXPECT_EQ(candidate->battleGetUnitByID(braced->unitId())->getAvailableHealth(), targetHealth);
			EXPECT_EQ(sibling->battleGetUnitByID(incoming->unitId())->getAvailableHealth(), actorBefore);
			EXPECT_EQ(sibling->battleGetUnitByID(braced->unitId())->getAvailableHealth(), targetBefore);
		}
		// Every query-bearing owner created above is helper-local and retired.
		// The immutable receipts/HP values below contain no providers or units.
		preview.reset();
		candidate.reset();
		player.reset();
		server.attacks.clear();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), controller,
			BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
		EXPECT_EQ(incoming->getAvailableHealth(), actorHealth);
		EXPECT_EQ(braced->getAvailableHealth(), targetHealth);
		if(brace)
		{
			ASSERT_EQ(server.attacks.size(), receipts.size());
			for(size_t index = 0; index < receipts.size(); ++index)
			{
				EXPECT_EQ(server.attacks[index].stackAttacking, receipts[index].attackerId);
				EXPECT_EQ(server.attacks[index].counter(), receipts[index].retaliation);
				ASSERT_EQ(server.attacks[index].bsa.size(), receipts[index].resolvedHits.size());
				for(size_t hit = 0; hit < receipts[index].resolvedHits.size(); ++hit)
				{
					EXPECT_EQ(server.attacks[index].bsa[hit].stackAttacked, receipts[index].resolvedHits[hit].first);
					EXPECT_EQ(server.attacks[index].bsa[hit].damageAmount, receipts[index].resolvedHits[hit].second);
				}
			}
		}
	}
};

TEST_F(BraceAttackPreviewTest, RealPlayerCandidateBraceUsesTheActualOrderedNativePrimaryHits)
{
	ASSERT_NO_FATAL_FAILURE(compareRealPreview(true));
}

TEST_F(BraceAttackPreviewTest, NoBraceKeepsTheExistingPlayerPreviewAndNativeHealthOutcome)
{
	ASSERT_NO_FATAL_FAILURE(compareRealPreview(false));
}

TEST_F(BraceAttackPreviewTest, BraceMetadataIsConsumedByTheActualSelectedReplayWithoutTouchingItsSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	releaseKnown();
	auto player = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto candidate = std::make_shared<HypotheticBattle>(environment.get(), player);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), candidate);
	const auto actorBefore = sibling->getForUpdate(incoming->unitId())->save();
	const auto targetBefore = sibling->getForUpdate(braced->unitId())->save();
	DamageCache cache;
	cache.buildDamageCache(candidate, BattleSide::DEFENDER);
	const auto preview = AttackPossibility::evaluate(
		BattleAttackInfo(candidate->battleGetUnitByID(incoming->unitId()),
			candidate->battleGetUnitByID(braced->unitId()), 3, false), BattleHex(92), cache, candidate);
	ASSERT_TRUE(preview.effectPreview);
	ASSERT_FALSE(preview.fortuneStrikes.empty());
	ASSERT_TRUE(preview.fortuneStrikes.front().bracePreemptive);
	BattleExchangeVariant exchange;
	exchange.trackAttack(preview, candidate, cache);
	EXPECT_EQ(candidate->battleGetUnitByID(incoming->unitId())->getAvailableHealth(),
		preview.attackerState->getAvailableHealth());
	EXPECT_EQ(candidate->battleGetUnitByID(braced->unitId())->getAvailableHealth(),
		preview.affectedUnits.front()->getAvailableHealth());
	EXPECT_EQ(sibling->getForUpdate(incoming->unitId())->save(), actorBefore);
	EXPECT_EQ(sibling->getForUpdate(braced->unitId())->save(), targetBefore);
}

TEST_F(BraceAttackPreviewTest, HypnotizedPhysicalEnemyUsesControllingOwnerBraceInPreviewReplayAndNative)
{
	ASSERT_NO_FATAL_FAILURE(compareRealPreview(true, true));
}

TEST_F(BraceAttackPreviewTest, HypnotizedDefenderReadinessDoesNotAuthorizeAnIllegalIncomingAction)
{
	ASSERT_NO_FATAL_FAILURE(prepareControlledBrace(false));
	const auto actorHealth = incoming->getAvailableHealth();
	const auto targetHealth = braced->getAvailableHealth();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	ASSERT_FALSE(battle()->battleCanAttackUnitAction(incoming, braced));
	{
		auto player = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
		auto candidate = std::make_shared<HypotheticBattle>(environment.get(), player);
		auto actor = candidate->getForUpdate(incoming->unitId());
		auto target = candidate->getForUpdate(braced->unitId());
		ASSERT_TRUE(candidate->battleCanTriggerHeroOrderBrace(actor.get(), target.get(), 3, false, false));
		const auto request = attackPreview::preparePreemptiveStage(attackPreview::PreemptiveStage::BRACE,
			*candidate, BattleAttackInfo(actor.get(), target.get(), 3, false), true,
			*actor, *target, 0, candidate->battleGetRound());
		ASSERT_TRUE(request); // Native reaction predicate, not base-action permission.
		EXPECT_TRUE(request->brace);
		DamageCache cache;
		cache.buildDamageCache(candidate, BattleSide::DEFENDER);
		PotentialTargets legal(actor.get(), cache, candidate);
		EXPECT_TRUE(std::ranges::none_of(legal.possibleAttacks, [&](const AttackPossibility & attack)
		{
			return attack.attack.defender->unitId() == braced->unitId();
		}));
	}
	server.attacks.clear();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
	EXPECT_TRUE(server.attacks.empty());
	EXPECT_EQ(incoming->getAvailableHealth(), actorHealth);
	EXPECT_EQ(braced->getAvailableHealth(), targetHealth);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}
