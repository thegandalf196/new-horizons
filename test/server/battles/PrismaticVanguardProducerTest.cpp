/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PrismaticVanguardFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
namespace vanguard = newHorizonsPrismaticVanguard;
class PrismaticVanguardProducerTest : public PrismaticVanguardFixture {};
TEST_F(PrismaticVanguardProducerTest, NativeAcceptedDefendEndsFirstActivationWithoutRearming)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_EQ(markers(*actor)->size(), 1u);
	ASSERT_EQ(markers(*actor)->front()->duration, BonusDuration::STACK_ACTIVATION);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(actor)));
	EXPECT_TRUE(markers(*actor)->empty());
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(markers(*actor)->empty());
	EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(actor, victim, 0, false)), 0);
}
TEST_F(PrismaticVanguardProducerTest, NativeContinuationRoutingPreservesPendingAndActiveWithoutDuplicateGrant)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const bool active : {false, true})
	{
		if(active) battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		const auto before = markers(*actor)->toJsonNode();
		for(const auto reason : {BattleUnitTurnReason::ACTION_REJECTED, BattleUnitTurnReason::MASTER_GATE_CONTINUATION,
			BattleUnitTurnReason::PURSUIT_CONTINUATION, BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION,
			BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION, BattleUnitTurnReason::HERO_SPELLCAST,
			BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::CRISIS_ORDER, BattleUnitTurnReason::CRISIS_RESUME})
		{
			battle()->nextTurn(actor->unitId(), reason);
			EXPECT_EQ(markers(*actor)->toJsonNode(), before);
		}
	}
}
TEST_F(PrismaticVanguardProducerTest, NativeTimeStopSyntheticSlotAndRoundPreservePendingUntilGenuineStart)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(actor->isTimeStopped());
	ASSERT_FALSE(battle()->battleBeginsActivation(actor, BattleUnitTurnReason::AUTOMATIC_ACTION));
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION);
	ASSERT_EQ(markers(*actor)->size(), 1u);
	EXPECT_EQ(markers(*actor)->front()->duration, BonusDuration::ONE_BATTLE);
	actor->removeBonusesRecursive(Selector::type()(BonusType::TIME_STOP));
	advanceRound();
	ASSERT_EQ(markers(*actor)->size(), 1u);
	EXPECT_EQ(markers(*actor)->front()->duration, BonusDuration::ONE_BATTLE);
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_EQ(markers(*actor)->size(), 1u);
	EXPECT_EQ(markers(*actor)->front()->duration, BonusDuration::STACK_ACTIVATION);
}
TEST_F(PrismaticVanguardProducerTest, NativePreparedSameIdRebirthRetainsExistingPendingAndNeverRearmsSpentBody)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const bool spent : {false, true})
	{
		auto * body = spent ? upgraded : actor;
		if(spent)
		{
			battle()->nextTurn(body->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			body->removeBonuses(CSelector(Bonus::UntilActivationEnds));
		}
		body->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::REBIRTH,
			BonusSource::OTHER, 100, BonusSourceID()));
		body->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CASTS,
			BonusSource::OTHER, 1, BonusSourceID()));
		const auto id = body->unitId();
		ASSERT_TRUE(body->canCast());
		BattleStackAttacked hit; hit.stackAttacked = id; hit.attackerID = victim->unitId();
		hit.damageAmount = body->getAvailableHealth();
		body->prepareAttacked(hit, gameHandler->getRandomGenerator());
		ASSERT_TRUE(hit.killed()); ASSERT_TRUE(hit.willRebirth());
		StacksInjured packet; packet.battleID = BattleID(0); packet.stacks.push_back(hit);
		gameHandler->sendAndApply(packet);
		ASSERT_EQ(battle()->getStack(id), body); ASSERT_TRUE(body->alive());
		EXPECT_EQ(markers(*body)->size(), spent ? 0u : 1u);
		battle()->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(body, victim, 0, false)), spent ? 0 : 20);
	}
}
class HistoricalVanguardProducerTest : public PrismaticVanguardFixture,
	public ::testing::WithParamInterface<int> {};
TEST_P(HistoricalVanguardProducerTest, HistoricalAndZeroIdentityAlsoApplyToRealFreshAddWithoutBenefit)
{
	capture = GetParam(); ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero), capture == 0);
	for(const auto * original : {actor, upgraded})
	{
		auto * fresh = addStack(BattleSide::ATTACKER, original->creatureId(), BattleHex(12, 4), 3);
		ASSERT_NE(fresh, nullptr); EXPECT_TRUE(markers(*fresh)->empty());
		battle()->nextTurn(fresh->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_TRUE(markers(*fresh)->empty());
		EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(fresh, victim, 0, false)), 0);
		battle()->removeUnit(fresh->unitId());
	}
}
INSTANTIATE_TEST_SUITE_P(HistoricalAndZero, HistoricalVanguardProducerTest, ::testing::Values(-1, 0));
class VanguardSourceWorldTest : public PrismaticVanguardFixture,
	public ::testing::WithParamInterface<int> {};
TEST_P(VanguardSourceWorldTest, Explicit1152WorldAndDescriptorPreservePendingActiveOrSpentWithoutLocalInitRearm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	if(GetParam() > 0) battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	if(GetParam() == 2) actor->removeBonuses(CSelector(Bonus::UntilActivationEnds));
	const auto before = markers(*actor)->toJsonNode();
	const auto capturedPolicy = attackerSideHero->getPrimaryGrowthRules();
	NativeBonusGraphAccess graph;
	// Installed default CURRENT remains1151 and must reject this captured key before its prefix.
	CMemorySerializer current;
	ASSERT_EQ(current.oser.version, ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	EXPECT_THROW(current.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(current.extractBuffer().empty());
	CMemorySerializer rejectedDescriptor;
	EXPECT_THROW(battleStartFixture::writeDescriptors(*battle(), rejectedDescriptor), std::runtime_error);
	EXPECT_TRUE(rejectedDescriptor.extractBuffer().empty());
	constexpr auto version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	CMemorySerializer world; world.oser.version = version;
	ASSERT_NO_THROW(world.oser & *gameState());
	CGameState restored; restored.preInit(LIBRARY);
	world.iser.version = version; world.iser.loadingGamestate = true; world.iser.cb = &restored;
	ASSERT_NO_THROW(world.iser & restored);
	const auto * hero = restored.getHero(attackerSideHero->id); ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), capturedPolicy);
	ASSERT_EQ(restored.getBattle(BattleID(0)), nullptr); // World codec omits live combats.
	CMemorySerializer descriptors; descriptors.oser.version = version;
	ASSERT_NO_THROW(battleStartFixture::writeDescriptors(*battle(), descriptors));
	descriptors.iser.version = version; descriptors.iser.cb = &restored;
	auto combat = std::make_unique<BattleInfo>(&restored);
	ASSERT_NO_THROW(descriptors.iser & *combat);
	auto * loaded = combat->getStack(actor->unitId(), false); ASSERT_NE(loaded, nullptr);
	BonusList exported; loaded->getExportedBonusList().getBonuses(exported, CSelector(vanguard::isEffect));
	EXPECT_EQ(exported.toJsonNode(), before);
	ASSERT_NO_THROW(combat->localInit());
	EXPECT_EQ(markers(*loaded)->toJsonNode(), before);
	const auto * target = combat->getStack(victim->unitId(), false); ASSERT_NE(target, nullptr);
	EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(loaded, target, 0, false)), GetParam() == 1 ? 20 : 0);
	if(GetParam() != 1)
	{
		combat->nextTurn(loaded->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(loaded, target, 0, false)), GetParam() == 0 ? 20 : 0);
	}
}
INSTANTIATE_TEST_SUITE_P(PendingActiveAndSpent, VanguardSourceWorldTest, ::testing::Values(0, 1, 2));
TEST_F(PrismaticVanguardProducerTest, HistoricalAbsenceKeepsCurrentWorldAndDescriptorCodecWithoutRearming)
{
	capture = -1; ASSERT_NO_FATAL_FAILURE(prepare()); NativeBonusGraphAccess graph;
	CMemorySerializer world;
	ASSERT_NO_THROW(world.oser & *gameState());
	CGameState restored; restored.preInit(LIBRARY);
	world.iser.loadingGamestate = true; world.iser.cb = &restored;
	ASSERT_NO_THROW(world.iser & restored);
	const auto * hero = restored.getHero(attackerSideHero->id); ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero));
	ASSERT_EQ(restored.getBattle(BattleID(0)), nullptr);
	CMemorySerializer descriptors;
	ASSERT_NO_THROW(battleStartFixture::writeDescriptors(*battle(), descriptors));
	descriptors.iser.cb = &restored;
	auto combat = std::make_unique<BattleInfo>(&restored);
	ASSERT_NO_THROW(descriptors.iser & *combat);
	ASSERT_NO_THROW(combat->localInit());
	auto * loaded = combat->getStack(actor->unitId(), false); ASSERT_NE(loaded, nullptr);
	EXPECT_TRUE(markers(*loaded)->empty());
	combat->nextTurn(loaded->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(markers(*loaded)->empty());
}
