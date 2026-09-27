/*
 * NewHorizonsRelentlessAssaultTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "HeroCommandFixture.h"

#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsOffense.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"

namespace
{
class NewHorizonsRelentlessAssaultTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void startBattleWithRelentlessAssault()
	{
		startGame();
		const int decodedOffense = SecondarySkill::decode(newHorizonsOffense::SKILL);
		ASSERT_GE(decodedOffense, 0);
		const SecondarySkill offense(decodedOffense);
		attackerSideHero->setSecSkillLevel(offense, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(
			newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT));
		startBattle();
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}

	static int64_t recordedDamage(const BattleAttack & attack)
	{
		int64_t result = 0;
		for(const auto & hit : attack.bsa)
			result += hit.damageAmount;
		return result;
	}
};
}

TEST_F(NewHorizonsRelentlessAssaultTest, SameTargetGetsTenPercentOnNextAlliedActivationAndPersists)
{
	ASSERT_NO_FATAL_FAILURE(startBattleWithRelentlessAssault());
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	BattleTestFixture::blockRetaliation(target);
	BattleTestFixture::forceMaximumDamage(attacker);
	ASSERT_NO_FATAL_FAILURE(beginCombat());

	server.attacks.clear();
	activate(attacker);
	ASSERT_TRUE(attack(attacker, target->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	const auto firstDamage = recordedDamage(server.attacks.back());
	ASSERT_GT(firstDamage, 0);
	const auto firstState = battle()->getRelentlessAssaultState(BattleSide::ATTACKER);
	EXPECT_EQ(firstState.targetUnitId, target->unitId());
	EXPECT_EQ(firstState.tier, 0);
	EXPECT_TRUE(firstState.activationHadEligibleAttack);

	// An opponent's turn does not break the hero-side streak.
	activate(target);
	activate(attacker);
	EXPECT_EQ(battle()->battleGetRelentlessAssaultDamagePercent(attacker, target), 10);
	ASSERT_TRUE(attack(attacker, target->getPosition()));
	ASSERT_EQ(server.attacks.size(), 2u);
	const auto secondDamage = recordedDamage(server.attacks.back());
	EXPECT_GT(secondDamage, firstDamage);
	EXPECT_EQ(battle()->getRelentlessAssaultState(BattleSide::ATTACKER).tier, 1);

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getRelentlessAssaultState(BattleSide::ATTACKER),
		battle()->getRelentlessAssaultState(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsRelentlessAssaultTest, BattleAndAttackPacketsRoundTripAndRejectOlderWriters)
{
	ASSERT_NO_FATAL_FAILURE(startBattleWithRelentlessAssault());
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	BattleTestFixture::blockRetaliation(target);
	ASSERT_NO_FATAL_FAILURE(beginCombat());

	server.attacks.clear();
	activate(attacker);
	ASSERT_TRUE(attack(attacker, target->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	ASSERT_TRUE(server.attacks.back().relentlessAssaultState.has_value());
	ASSERT_EQ(server.attacks.back().relentlessAssaultSide, BattleSide::ATTACKER);

	BattleStart outgoingStart;
	outgoingStart.battleID = BattleID(0);
	outgoingStart.info = CMemorySerializer::deepCopy(*battle(), gameState().get());
	CMemorySerializer startRoundTrip;
	startRoundTrip.oser.version = ESerializationVersion::CURRENT;
	startRoundTrip.iser.version = ESerializationVersion::CURRENT;
	startRoundTrip.oser & outgoingStart;
	startRoundTrip.iser.cb = gameState().get();
	BattleStart incomingStart;
	startRoundTrip.iser & incomingStart;
	ASSERT_NE(incomingStart.info, nullptr);
	EXPECT_EQ(incomingStart.info->getRelentlessAssaultState(BattleSide::ATTACKER),
		battle()->getRelentlessAssaultState(BattleSide::ATTACKER));

	auto outgoingAttack = server.attacks.back();
	CMemorySerializer attackRoundTrip;
	attackRoundTrip.oser.version = ESerializationVersion::CURRENT;
	attackRoundTrip.iser.version = ESerializationVersion::CURRENT;
	attackRoundTrip.oser & outgoingAttack;
	attackRoundTrip.iser.cb = gameState().get();
	BattleAttack incomingAttack;
	attackRoundTrip.iser & incomingAttack;
	ASSERT_TRUE(incomingAttack.relentlessAssaultState.has_value());
	EXPECT_EQ(incomingAttack.relentlessAssaultSide, BattleSide::ATTACKER);
	EXPECT_EQ(*incomingAttack.relentlessAssaultState,
		battle()->getRelentlessAssaultState(BattleSide::ATTACKER));

	CMemorySerializer oldBattleWriter;
	oldBattleWriter.oser.version = ESerializationVersion::NEW_HORIZONS_CLEAVE;
	EXPECT_THROW(oldBattleWriter.oser & *battle(), std::runtime_error);

	CMemorySerializer oldStartWriter;
	oldStartWriter.oser.version = ESerializationVersion::NEW_HORIZONS_CLEAVE;
	EXPECT_THROW(oldStartWriter.oser & outgoingStart, std::runtime_error);

	CMemorySerializer oldAttackWriter;
	oldAttackWriter.oser.version = ESerializationVersion::NEW_HORIZONS_CLEAVE;
	EXPECT_THROW(oldAttackWriter.oser & outgoingAttack, std::runtime_error);
}
