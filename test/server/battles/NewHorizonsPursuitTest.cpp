/*
 * NewHorizonsPursuitTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../AI/BattleAI/BattleAI.h"

namespace
{
class NewHorizonsPursuitTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	SecondarySkill offense() const
	{
		const int decoded = SecondarySkill::decode("new-horizons:offense");
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void prepare(bool selectPursuit, bool prepareSpellbook = false)
	{
		startGame();
		if(prepareSpellbook)
		{
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
			setTestSpellPointTotal(attackerSideHero, 100);
		}
		attackerSideHero->setSecSkillLevel(offense(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectPursuit)
		{
			attackerSideHero->applyPerkSelection(
				{"new-horizons:offense", "new-horizons:offense.pursuit"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(
				"new-horizons:offense", "new-horizons:offense.pursuit"));
		}
		startBattle();
	}

	bool act(const CStack * stack, const BattleAction & action)
	{
		battle()->activeStack = stack->unitId();
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), battle()->battleGetOwner(stack), action);
	}
};
}

TEST_F(NewHorizonsPursuitTest, LethalMeleePreservesOnlyUnusedMovementAndRejectsAnotherAttack)
{
	prepare(true);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(82), 1);
	auto * nextVictim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(83), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);

	const auto attackFrom = BattleHex(81);
	const int speed = attacker->getMovementRange(0);
	const int approachDistance = battle()->getPath(attacker->getPosition(), attackFrom, attacker).second;
	ASSERT_GT(approachDistance, 0);
	ASSERT_TRUE(act(attacker,
		BattleAction::makeMeleeAttack(attacker, victim->getPosition(), attackFrom)));
	EXPECT_FALSE(victim->alive());
	ASSERT_GT(attacker->pursuitMovementRemaining, 0);
	EXPECT_EQ(attacker->pursuitMovementRemaining, speed - approachDistance);
	EXPECT_EQ(battle()->getActiveStackID(), attacker->unitId());
	ASSERT_FALSE(server.stackActivations.empty());
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::PURSUIT_CONTINUATION);

	BattleClientInterfaceData clientData;
	const auto actions = battle()->getClientActionsForStack(attacker, clientData);
	ASSERT_EQ(actions.size(), 1u);
	EXPECT_EQ(actions.front(), PossiblePlayerBattleAction::MOVE_STACK);

	const auto forged = BattleAction::makeMeleeAttack(
		attacker, nextVictim->getPosition(), victim->getPosition());
	const int allowance = attacker->pursuitMovementRemaining;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), battle()->battleGetOwner(attacker), forged));
	EXPECT_EQ(attacker->pursuitMovementRemaining, allowance);
	EXPECT_EQ(battle()->getActiveStackID(), attacker->unitId());

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->battleGetOwner(attacker), BattleAction::makeMove(attacker, victim->getPosition())));
	EXPECT_EQ(attacker->getPosition(), victim->getPosition());
	EXPECT_EQ(attacker->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsPursuitTest, MovementBeyondSavedAllowanceIsRejectedWithoutClosingPursuit)
{
	prepare(true);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(83), 1);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	const auto attackFrom = BattleHex(82);
	const int speed = attacker->getMovementRange(0);
	const int approachDistance = battle()->getPath(attacker->getPosition(), attackFrom, attacker).second;
	ASSERT_GT(approachDistance, 0);
	ASSERT_TRUE(act(attacker,
		BattleAction::makeMeleeAttack(attacker, victim->getPosition(), attackFrom)));
	ASSERT_GT(attacker->pursuitMovementRemaining, 0);

	const int allowance = attacker->pursuitMovementRemaining;
	ASSERT_EQ(allowance, speed - approachDistance);
	attacker->pursuitMovementRemaining = 0;
	const auto ordinaryDestinations = battle()->battleGetAvailableHexes(attacker, false);
	attacker->pursuitMovementRemaining = allowance;
	const auto cappedDestinations = battle()->battleGetAvailableHexes(attacker, false);
	BattleHex tooFar = BattleHex::INVALID;
	const auto distances = battle()->battleGetDistances(attacker, attacker->getPosition());
	for(const auto candidate : ordinaryDestinations)
	{
		const auto distance = distances[candidate.toInt()];
		if(distance > allowance && distance <= speed)
		{
			tooFar = candidate;
			break;
		}
	}
	ASSERT_TRUE(tooFar.isAvailable());
	EXPECT_TRUE(ordinaryDestinations.contains(tooFar));
	EXPECT_FALSE(cappedDestinations.contains(tooFar));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->battleGetOwner(attacker), BattleAction::makeMove(attacker, tooFar)));
	EXPECT_EQ(attacker->pursuitMovementRemaining, allowance);
	EXPECT_EQ(battle()->getActiveStackID(), attacker->unitId());
}

TEST_F(NewHorizonsPursuitTest, IndependentHeroSpellRemainsUsableBeforePursuitMovement)
{
	prepare(true, true);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(81), 1);
	auto * spellTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(100), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	ASSERT_TRUE(act(attacker,
		BattleAction::makeMeleeAttack(attacker, victim->getPosition(), attacker->getPosition())));
	const int allowance = attacker->pursuitMovementRemaining;
	ASSERT_GT(allowance, 0);

	BattleAction spell;
	spell.actionType = EActionType::HERO_SPELL;
	spell.side = BattleSide::ATTACKER;
	spell.spell = SpellID::MAGIC_ARROW;
	spell.aimToUnit(spellTarget);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), battle()->battleGetOwner(attacker), spell));
	EXPECT_EQ(attacker->pursuitMovementRemaining, allowance);
	EXPECT_EQ(battle()->getActiveStackID(), attacker->unitId());

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->battleGetOwner(attacker), BattleAction::makeMove(attacker, victim->getPosition())));
	EXPECT_EQ(attacker->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsPursuitTest, DefendButtonDeclinesMovementWithoutGrantingDefense)
{
	prepare(true);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(81), 1);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	ASSERT_TRUE(act(attacker,
		BattleAction::makeMeleeAttack(attacker, victim->getPosition(), attacker->getPosition())));
	ASSERT_GT(attacker->pursuitMovementRemaining, 0);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->battleGetOwner(attacker), BattleAction::makeDefend(attacker)));
	EXPECT_EQ(attacker->pursuitMovementRemaining, 0);
	EXPECT_FALSE(attacker->defended());
	ASSERT_FALSE(server.startedActions.empty());
	EXPECT_EQ(server.startedActions.back().ba.actionType, EActionType::NO_ACTION);
}

TEST_F(NewHorizonsPursuitTest, LethalMeleeWithoutPerkDoesNotOpenContinuation)
{
	prepare(false);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(81), 1);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	ASSERT_TRUE(act(attacker,
		BattleAction::makeMeleeAttack(attacker, victim->getPosition(), attacker->getPosition())));
	EXPECT_FALSE(victim->alive());
	EXPECT_EQ(attacker->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsPursuitTest, NonlethalMeleeDoesNotOpenContinuation)
{
	prepare(true);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 1);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	blockRetaliation(attacker);
	ASSERT_TRUE(act(attacker,
		BattleAction::makeMeleeAttack(attacker, victim->getPosition(), attacker->getPosition())));
	EXPECT_TRUE(victim->alive());
	EXPECT_EQ(attacker->pursuitMovementRemaining, 0);
}

TEST_F(NewHorizonsPursuitTest, AllowanceRoundTripsAndARealNewActivationClearsIt)
{
	prepare(true);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	auto state = attacker->acquireState();
	state->pursuitMovementRemaining = 4;
	const JsonNode saved = state->save();
	EXPECT_EQ(saved["state"]["pursuitMovementRemaining"].Integer(), 4);
	auto restored = attacker->acquireState();
	ASSERT_NO_THROW(restored->load(saved));
	EXPECT_EQ(restored->pursuitMovementRemaining, 4);

	attacker->pursuitMovementRemaining = 4;
	battle()->nextTurn(attacker->unitId(), BattleUnitTurnReason::PURSUIT_CONTINUATION);
	EXPECT_EQ(attacker->pursuitMovementRemaining, 4);
	battle()->nextTurn(attacker->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(attacker->pursuitMovementRemaining, 0);
}

TEST(NewHorizonsPursuitWireTest, ContinuationRequiresThePursuitProtocol)
{
	BattleSetActiveStack outgoing;
	outgoing.battleID = BattleID(7);
	outgoing.stack = 19;
	outgoing.reason = BattleUnitTurnReason::PURSUIT_CONTINUATION;
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & outgoing);
	BattleSetActiveStack restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.reason, BattleUnitTurnReason::PURSUIT_CONTINUATION);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MASTER_GATE;
	EXPECT_THROW(old.oser & outgoing, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST_F(NewHorizonsPursuitTest, BattleAIChoosesMovementWithinAllowanceAndNeverAnotherAttack)
{
	prepare(true);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(100), 100);
	attacker->pursuitMovementRemaining = 2;
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](CBattleInfoCallback *) {});
	const auto action = CBattleAI::choosePursuitMovement(callback, attacker);
	ASSERT_EQ(action.actionType, EActionType::WALK);
	const auto target = action.getTarget(battle());
	ASSERT_EQ(target.size(), 1u);
	const auto [path, distance] = battle()->getPath(
		attacker->getPosition(), target.front().hexValue, attacker);
	EXPECT_FALSE(path.empty());
	EXPECT_GT(distance, 0);
	EXPECT_LE(distance, attacker->pursuitMovementRemaining);
}
