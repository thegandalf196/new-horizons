/*
 * SelectedAttackReceiptTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequenceFixture.h"
#include "../../AI/BattleAI/VisibleCandidateWorld.h"
#include "../../AI/BattleAI/SelectedAttackReceipt.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include <bit>

class SelectedAttackReceiptTest : public OriginalMeleeSequenceTest
{
protected:
	std::optional<AttackPossibility> preview;
	void prepareVisibleCandidate(bool recipientBonus = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
		releaseKnown();
		auto player = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
		known = std::make_shared<HypotheticBattle>(environment.get(), player);
		if(recipientBonus)
			known->getForUpdate(incoming->unitId())->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
				BonusType::PRIMARY_SKILL, BonusSource::OTHER, 4, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK))});
		DamageCache cache;
		PotentialTargets targets(known->battleGetUnitByID(incoming->unitId()), cache, known);
		ASSERT_FALSE(targets.possibleAttacks.empty());
		preview = targets.bestAction();
		ASSERT_TRUE(preview->queryWorld);
	}
};

TEST_F(SelectedAttackReceiptTest, CollectionPreservesOccurrenceOrderAndLastWriteWinsWithoutInferringDamageProvenance)
{
	ASSERT_NO_FATAL_FAILURE(prepareVisibleCandidate());
	ASSERT_FALSE(preview->affectedUnits.empty());
	// A duplicate is an existing representable preview, not a deduplicated graph.
	auto duplicate = preview->affectedUnits.front()->acquireState();
	duplicate->health.addTemporaryHitPoints(7); // Local ledger only; no borrowed-environment damage call.
	preview->affectedUnits.push_back(duplicate);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), known);
	SelectedAttackReceipt receipt(*preview);
	receipt.captureDamage(*branch);
	ASSERT_EQ(receipt.affectedUnits().size(), preview->affectedUnits.size() + 1);
	ASSERT_EQ(receipt.healthObservations().size(), receipt.affectedUnits().size());
	EXPECT_EQ(receipt.affectedUnits().back(), preview->attackerState);
	std::map<uint32_t, int64_t> expected;
	for(size_t index = 0; index < receipt.affectedUnits().size(); ++index)
	{
		const auto & snapshot = receipt.affectedUnits()[index];
		const auto & observed = receipt.healthObservations()[index];
		EXPECT_EQ(snapshot, index < preview->affectedUnits.size() ? preview->affectedUnits[index] : preview->attackerState);
		EXPECT_EQ(observed.id, snapshot->unitId());
		EXPECT_EQ(observed.beforeHealth, branch->battleGetUnitByID(observed.id)->getAvailableHealth());
		EXPECT_EQ(observed.previewHealth, snapshot->getAvailableHealth());
		expected[observed.id] = std::max<int64_t>(0, observed.beforeHealth - observed.previewHealth);
	}
	EXPECT_EQ(receipt.damage(), expected);
	EXPECT_EQ(receipt.sourceOwner(), preview->queryWorld);
	EXPECT_EQ(receipt.effectPreviewOwner(), preview->effectPreview);
	EXPECT_EQ(receipt.sourceOwner()->heroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
}

TEST_F(SelectedAttackReceiptTest, ActualReplayKeepsTheSameProvidersAndSeparatesSiblingRecipientHistories)
{
	ASSERT_NO_FATAL_FAILURE(prepareVisibleCandidate(true));
	const auto nativeState = incoming->acquireState()->save();
	const auto sourceState = known->battleGetUnitByID(incoming->unitId())->acquireState()->save();
	const auto snapshotState = preview->attackerState->save();
	const auto * provider = preview->attackerState->getBonusBearer();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto first = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
	const auto siblingBefore = sibling->battleGetUnitByID(braced->unitId())->acquireState()->save();
	DamageCache firstCache, siblingCache;
	BattleExchangeVariant firstExchange, siblingExchange;
	const auto score = firstExchange.trackAttack(*preview, first, firstCache);
	EXPECT_EQ(sibling->battleGetUnitByID(braced->unitId())->acquireState()->save(), siblingBefore);
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->acquireState()->save(), sourceState);
	EXPECT_EQ(incoming->acquireState()->save(), nativeState);
	EXPECT_EQ(preview->attackerState->getBonusBearer(), provider);
	EXPECT_EQ(preview->attackerState->save(), snapshotState);
	const auto secondScore = siblingExchange.trackAttack(*preview, sibling, siblingCache);
	EXPECT_EQ(std::bit_cast<uint32_t>(score), std::bit_cast<uint32_t>(secondScore));
	for(const auto * original : {incoming, braced})
		EXPECT_EQ(first->battleGetUnitByID(original->unitId())->acquireState()->save(),
			sibling->battleGetUnitByID(original->unitId())->acquireState()->save());
	EXPECT_EQ(firstExchange.getScore().ourDamageReduce, siblingExchange.getScore().ourDamageReduce);
	EXPECT_EQ(firstExchange.getScore().enemyDamageReduce, siblingExchange.getScore().enemyDamageReduce);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->getAttack(false), incoming->getAttack(false) + 4);
	EXPECT_EQ(known->battleGetUnitByID(braced->unitId())->getAttack(false), braced->getAttack(false));
}

TEST_F(SelectedAttackReceiptTest, ResourceTransferKeepsRecipientCachesAndDoesNotApplyHealthOrInventExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepareVisibleCandidate());
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto recipient = branch->getForUpdate(incoming->unitId());
	auto captured = preview->attackerState->acquireState();
	captured->physicalPoisonBaseDamage = 13;
	captured->archeryCounterfireRound = 5;
	captured->veteranPhysicalDamageSinceActivation = 27;
	captured->rangedFollowUpDamagePercent = 40;
	const auto health = recipient->getAvailableHealth();
	const auto * provider = recipient->getBonusBearer();
	const auto sourceState = known->battleGetUnitByID(incoming->unitId())->acquireState()->save();
	selectedAttackReceipt::restoreResources(*preview, *captured, recipient, recipient, *branch);
	EXPECT_EQ(recipient->physicalPoisonBaseDamage, 13);
	EXPECT_EQ(recipient->archeryCounterfireRound, 5);
	EXPECT_EQ(recipient->veteranPhysicalDamageSinceActivation, 27);
	EXPECT_EQ(recipient->rangedFollowUpDamagePercent, preview->attack.shooting && preview->attack.physicalDamage ? 0 : 40);
	EXPECT_EQ(recipient->shots.available(), captured->shots.available());
	EXPECT_EQ(recipient->counterAttacks.available(), captured->counterAttacks.available());
	EXPECT_EQ(recipient->getAvailableHealth(), health);
	EXPECT_EQ(recipient->getBonusBearer(), provider);
	EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->acquireState()->save(), sourceState);
}

TEST_F(SelectedAttackReceiptTest, ReceiptRetainsActualEffectAndQueryOwnersButDoesNotCreateAnExportWorld)
{
	ASSERT_NO_FATAL_FAILURE(prepareVisibleCandidate());
	auto effects = std::make_shared<HypotheticBattle>(environment.get(), known);
	preview->effectPreview = effects;
	preview->attackerState = effects->getForUpdate(incoming->unitId());
	preview->affectedUnits = {effects->getForUpdate(braced->unitId())};
	std::weak_ptr<HypotheticBattle> effectLifetime = effects;
	std::weak_ptr<HypotheticBattle> queryLifetime = known;
	std::optional<SelectedAttackReceipt> receipt;
	receipt.emplace(*preview);
	const auto * provider = receipt->affectedUnits().front()->getBonusBearer();
	preview.reset();
	effects.reset();
	releaseKnown();
	ASSERT_FALSE(effectLifetime.expired());
	ASSERT_FALSE(queryLifetime.expired());
	EXPECT_EQ(receipt->affectedUnits().front()->getBonusBearer(), provider);
	EXPECT_EQ(receipt->effectPreviewOwner()->battleGetUnitByID(braced->unitId()), receipt->affectedUnits().front().get());
	EXPECT_FALSE(receipt->effectPreviewOwner()->initialExportSubject());
	EXPECT_EQ(receipt->sourceOwner()->heroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	receipt.reset();
	EXPECT_TRUE(effectLifetime.expired());
	EXPECT_TRUE(queryLifetime.expired()); // Environment/library interval remains fixture-owned.
}
