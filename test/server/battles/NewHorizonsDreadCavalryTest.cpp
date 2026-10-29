/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsDreadCavalryFixture.h"
using NewHorizonsDreadCavalryTest = NewHorizonsDreadCavalryFixture;

TEST_F(NewHorizonsDreadCavalryTest, NativeKillArmsOneSameActivationChoiceWithUnusedMovement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int initialMovement = source->getMovementRange(0);
	ASSERT_TRUE(killFirst());
	ASSERT_FALSE(victim->alive());
	ASSERT_TRUE(battle()->battleHasPendingDreadCavalry(source));
	EXPECT_TRUE(source->dreadCavalryState.usedThisActivation);
	EXPECT_EQ(source->dreadCavalryState.victimId, victim->unitId());
	EXPECT_EQ(battle()->battleGetDreadCavalryMovementRemaining(source), initialMovement - 1);
	EXPECT_EQ(battle()->battleGetDreadCavalryDamagePercent(source), 50);
	EXPECT_FALSE(battle()->battleCanAttackUnitAction(source, victim));
	EXPECT_TRUE(battle()->battleCanAttackUnitAction(source, other));
}
TEST_F(NewHorizonsDreadCavalryTest, AcceptedFollowupConsumesGrantAndDoesNotGrantAnotherActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(source, other, BattleHex(8, 5), false)));
	EXPECT_FALSE(source->dreadCavalryState.pending);
	EXPECT_TRUE(source->dreadCavalryState.usedThisActivation);
	EXPECT_EQ(battle()->battleGetDreadCavalryDamagePercent(source), 100);
}
TEST_F(NewHorizonsDreadCavalryTest, DeclineRetainsUsedAndNoDefendOrWaitBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	const bool defended = source->defended();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeNoAction(source)));
	EXPECT_FALSE(source->dreadCavalryState.pending);
	EXPECT_TRUE(source->dreadCavalryState.usedThisActivation);
	EXPECT_EQ(source->defended(), defended);
}
TEST_F(NewHorizonsDreadCavalryTest, PreviewUsesOneOutputStageAndReactionStaysFullStrength)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	BattleAttackInfo preview(source, other, 0, false);
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(preview), 50);
	preview.dreadCavalryDamagePercent = 50;
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(preview), 50); // Never25%.
	preview.retaliation = true;
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(preview), 100);
	preview.retaliation = false;
	preview.bracePreemptive = true;
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(preview), 100);
}
TEST_F(NewHorizonsDreadCavalryTest, HistoricalAbsentCaptureRetainsOrdinaryKnightBehavior)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	EXPECT_FALSE(source->dreadCavalryState.hasState());
	EXPECT_FALSE(battle()->battleHasPendingDreadCavalry(source));
}

TEST_F(NewHorizonsDreadCavalryTest, DirectWaitIsRejectedAndDirectDefendOnlyDeclinesCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	const auto pending = source->dreadCavalryState;
	ASSERT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeWait(source)));
	EXPECT_EQ(source->dreadCavalryState, pending);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(source)));
	EXPECT_FALSE(source->dreadCavalryState.pending);
	EXPECT_FALSE(source->defended());
}
TEST_F(NewHorizonsDreadCavalryTest, ZeroRemainingMovementStillPermitsNativeAdjacentMelee)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	source->dreadCavalryState.remainingMovement = 0;
	battle()->moveUnit(other->unitId(), BattleHex(7, 5));
	ASSERT_TRUE(battle()->battleHasPendingDreadCavalry(source));
	ASSERT_TRUE(battle()->battleCanAttackUnitAction(source, other));
	const auto position = source->getPosition();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(source, other, position, false)));
	EXPECT_EQ(source->getPosition(), position);
	EXPECT_FALSE(source->dreadCavalryState.pending);
}
TEST_F(NewHorizonsDreadCavalryTest, OriginalVictimIdentityStaysExcludedAfterRestoration)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto originalVictim = victim->save();
	ASSERT_TRUE(killFirst());
	auto restored = victim->acquireState();
	restored->load(originalVictim);
	BattleUnitsChanged changed;
	changed.battleID = BattleID(0);
	UnitChanges update(victim->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = restored->save();
	changed.changedStacks.push_back(update);
	gameHandler->sendAndApply(changed);
	ASSERT_TRUE(victim->alive());
	EXPECT_FALSE(battle()->battleCanAttackUnitAction(source, victim));
	const auto position = source->getPosition();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(source, victim, position, false)));
	EXPECT_EQ(source->getPosition(), position);
	EXPECT_TRUE(source->dreadCavalryState.pending);
}
TEST_F(NewHorizonsDreadCavalryTest, AcquiredAndDeadStatesRetainOncePerActivationReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	auto copy = source->acquireState();
	ASSERT_EQ(copy->dreadCavalryState, source->dreadCavalryState);
	copy->makeGhost();
	EXPECT_TRUE(copy->dreadCavalryState.usedThisActivation);
	EXPECT_FALSE(copy->dreadCavalryState.pending);
	EXPECT_TRUE(source->dreadCavalryState.pending);
}
TEST_F(NewHorizonsDreadCavalryTest, ContinuationAndCrisisResumeRetainStateGenuineActivationResetsIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	const auto pending = source->dreadCavalryState;
	for(const auto reason : {BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION, BattleUnitTurnReason::CRISIS_RESUME})
	{
		BattleSetActiveStack packet;
		packet.battleID = BattleID(0);
		packet.stack = source->unitId();
		packet.reason = reason;
		gameHandler->sendAndApply(packet);
		EXPECT_EQ(source->dreadCavalryState, pending);
	}
	BattleSetActiveStack packet;
	packet.battleID = BattleID(0);
	packet.stack = source->unitId();
	packet.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(packet);
	EXPECT_FALSE(source->dreadCavalryState.hasState());
}
TEST_F(NewHorizonsDreadCavalryTest, ChangedControllerCannotSpendPreviousOwnersCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::HYPNOTIZE))));
	EXPECT_TRUE(source->dreadCavalryState.pending);
	EXPECT_FALSE(battle()->battleHasPendingDreadCavalry(source));
	EXPECT_FALSE(battle()->battleCanAttackUnitAction(source, other));
	EXPECT_EQ(battle()->battleGetDreadCavalryDamagePercent(source), 100);
}

TEST_F(NewHorizonsDreadCavalryTest, VictimNativeRebirthDoesNotCountAsDestruction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(victim->canCast());
	ASSERT_TRUE(killFirst());
	ASSERT_TRUE(victim->alive());
	EXPECT_FALSE(source->dreadCavalryState.hasState());
	EXPECT_TRUE(std::ranges::any_of(server.attacks, [&](const auto & attack)
	{
		return std::ranges::any_of(attack.bsa, [&](const auto & hit)
			{ return hit.stackAttacked == victim->unitId() && hit.willRebirth(); });
	}));
}
TEST_F(NewHorizonsDreadCavalryTest, AcceptedDoubleStrikeRetainsOutputAndUsedThroughNativeRebirth)
{
	allowRetaliation = true;
	opposingCount = 100000;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	forceMaximumDamage(other);
	ASSERT_EQ(source->getTotalAttacks(false), 2);
	ASSERT_TRUE(source->canCast());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(source, other, BattleHex(8, 5), false)));
	std::vector<int64_t> outgoing;
	bool reborn = false;
	for(const auto & attack : server.attacks)
		for(const auto & hit : attack.bsa)
		{
			if(hit.stackAttacked == source->unitId() && hit.willRebirth())
				reborn = true;
			if(attack.stackAttacking == source->unitId() && hit.stackAttacked == other->unitId())
				outgoing.push_back(hit.damageAmount);
		}
	ASSERT_TRUE(reborn);
	ASSERT_EQ(outgoing.size(), 2);
	EXPECT_EQ(outgoing[0], outgoing[1]); // Rebirth cannot restore100% for the accepted second hit.
	EXPECT_TRUE(source->alive());
	EXPECT_TRUE(source->dreadCavalryState.usedThisActivation);
	EXPECT_FALSE(source->dreadCavalryState.pending);
}

TEST_F(NewHorizonsDreadCavalryTest, SameActorHeroOrderRetainsPendingChargeAndCurrentUnitWire)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	const auto pending = source->dreadCavalryState;
	EXPECT_FALSE(newHorizonsDreadCavalry::abandonsPendingOnActorHandoff(*battle(), source->unitId(), BattleUnitTurnReason::HERO_COMMAND));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE)));
	ASSERT_EQ(battle()->battleActiveUnit(), source);
	EXPECT_EQ(source->dreadCavalryState, pending);
	EXPECT_NO_THROW(battle()->validateDreadCavalryStateSerialization(true));
	UnitChanges change(source->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = source->save();
	CMemorySerializer bytes;
	bytes.oser & change;
	UnitChanges restored;
	bytes.iser & restored;
	EXPECT_EQ(battle::dreadCavalryStateFromUnitJson(restored.data), pending);
}
TEST_F(NewHorizonsDreadCavalryTest, OrdinarySecondWindRelinquishesOnlyChargeAndRetainsSafeState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(2, 8), 100);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = reserve->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(reserve)));
	activate.stack = source->unitId();
	gameHandler->sendAndApply(activate);
	ASSERT_TRUE(killFirst());
	source->pursuitMovementRemaining = 3; // Independently earned legacy tail must not be erased.
	EXPECT_TRUE(newHorizonsDreadCavalry::abandonsPendingOnActorHandoff(*battle(), reserve->unitId(), BattleUnitTurnReason::HERO_COMMAND));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, reserve->unitId())));
	ASSERT_EQ(battle()->battleActiveUnit(), reserve);
	EXPECT_FALSE(source->dreadCavalryState.pending);
	EXPECT_TRUE(source->dreadCavalryState.usedThisActivation);
	EXPECT_EQ(source->pursuitMovementRemaining, 3);
	EXPECT_NO_THROW(battle()->validateDreadCavalryStateSerialization(true));
	UnitChanges change(source->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = source->save();
	CMemorySerializer bytes;
	bytes.oser & change;
	UnitChanges restored;
	bytes.iser & restored;
	EXPECT_EQ(battle::dreadCavalryStateFromUnitJson(restored.data), source->dreadCavalryState);
}
TEST_F(NewHorizonsDreadCavalryTest, GenuineCrisisSuspensionAndResumeRetainOriginalChargeWithoutReplay)
{
	defenderCrisis = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	ASSERT_TRUE(battle()->getCrisisCommandState().choice());
	ASSERT_NE(battle()->battleActiveUnit(), source);
	ASSERT_EQ(battle()->getCrisisCommandState().returns.back().originalActor, static_cast<int32_t>(source->unitId()));
	const auto pending = source->dreadCavalryState;
	ASSERT_TRUE(pending.pending);
	EXPECT_NO_THROW(battle()->validateDreadCavalryStateSerialization(true));
	EXPECT_FALSE(newHorizonsDreadCavalry::abandonsPendingOnActorHandoff(*battle(), source->unitId(), BattleUnitTurnReason::CRISIS_ORDER));
	const auto attacks = server.attacks.size();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeHeroCommand(BattleSide::DEFENDER, HeroCommand::HOLD_THE_LINE)));
	EXPECT_TRUE(battle()->getCrisisCommandState().returns.empty());
	ASSERT_EQ(battle()->battleActiveUnit(), source);
	EXPECT_EQ(source->dreadCavalryState, pending);
	EXPECT_EQ(server.attacks.size(), attacks);
	EXPECT_TRUE(battle()->battleHasPendingDreadCavalry(source));
	EXPECT_NO_THROW(battle()->validateDreadCavalryStateSerialization(true));
}

TEST_F(NewHorizonsDreadCavalryTest, PhysicalCollateralKillQualifiesEvenWhenPrimarySurvives)
{
	primaryCount = 100000;
	opposingCount = 1;
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->moveUnit(other->unitId(), BattleHex(8, 5));
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::TWO_HEX_ATTACK_BREATH, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(killFirst());
	ASSERT_TRUE(victim->alive());
	ASSERT_FALSE(other->alive());
	ASSERT_TRUE(source->dreadCavalryState.pending);
	EXPECT_EQ(source->dreadCavalryState.victimId, other->unitId());
	EXPECT_TRUE(battle()->battleCanAttackUnitAction(source, victim));
}

// Exercise the actual descriptor decoder followed by the ordinary graph rebind,
// not just DreadCavalryState's standalone codec.
class NewHorizonsDreadCavalryInitializationTest : public NewHorizonsDreadCavalryFixture
{
protected:
	void expectDescriptorRebind(const battle::DreadCavalryState & expected,
		ESerializationVersion epoch = ESerializationVersion::CURRENT)
	{
		CStackBasicDescriptor base(creatureByName("core:blackKnight"), 10);
		CStack descriptor(&base, PlayerColor(0), 100, BattleSide::ATTACKER);
		descriptor.initialPosition = BattleHex(5, 5);
		descriptor.dreadCavalryState = expected;
		descriptor.forestAmbush.attacked = true;
		descriptor.petrifyingBloodRound = 3;
		CMemorySerializer bytes;
		bytes.oser.version = bytes.iser.version = epoch;
		ASSERT_NO_THROW(bytes.oser & descriptor);
		CStack loaded;
		// Old readers must clear a previously nonempty destination too.
		loaded.dreadCavalryState.arm(7, BattleSide::ATTACKER, 6, 50, 4, 2);
		bytes.iser.cb = gameState().get();
		ASSERT_NO_THROW(bytes.iser & loaded);
		ASSERT_EQ(loaded.dreadCavalryState, expected);
		ASSERT_TRUE(loaded.forestAmbush.attacked);
		ASSERT_EQ(loaded.petrifyingBloodRound, 3);
		const auto decodedForest = loaded.forestAmbush;
		const auto decodedPetrifying = loaded.petrifyingBloodRound;
		ASSERT_NO_THROW(loaded.localInit(battle()));
		EXPECT_EQ(loaded.dreadCavalryState, expected);
		EXPECT_EQ(loaded.forestAmbush, decodedForest);
		EXPECT_EQ(loaded.petrifyingBloodRound, decodedPetrifying);
		EXPECT_EQ(loaded.getPosition(), descriptor.initialPosition);
		if(expected.usedThisActivation)
			EXPECT_THROW(loaded.dreadCavalryState.arm(8, BattleSide::ATTACKER, 3, 50, 3, 0), std::runtime_error);
	}
};

TEST_F(NewHorizonsDreadCavalryInitializationTest, DecodedPendingDescriptorRetainsEveryDreadFieldThroughLocalInit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle::DreadCavalryState expected;
	expected.arm(victim->unitId(), BattleSide::ATTACKER, 6, 50, 4, 2);
	ASSERT_TRUE(expected.pending);
	ASSERT_TRUE(expected.usedThisActivation);
	ASSERT_NO_FATAL_FAILURE(expectDescriptorRebind(expected));
}
TEST_F(NewHorizonsDreadCavalryInitializationTest, DecodedUsedOnlyDescriptorCannotRearmAfterLocalInit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle::DreadCavalryState expected;
	expected.arm(victim->unitId(), BattleSide::ATTACKER, 6, 50, 4, 2);
	expected.retirePending();
	ASSERT_FALSE(expected.pending);
	ASSERT_TRUE(expected.usedThisActivation);
	ASSERT_NO_FATAL_FAILURE(expectDescriptorRebind(expected));
}
TEST_F(NewHorizonsDreadCavalryInitializationTest, PreDreadDescriptorStaysAbsentAfterDecodeAndLocalInit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(expectDescriptorRebind({}, ESerializationVersion::NEW_HORIZONS_AERIAL_SUPREMACY));
}
