/*
 * NewHorizonsBattlecraftPreemptiveStrikeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/CStack.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/BattleChanges.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
constexpr auto battlecraftSkill = "new-horizons:battlecraft";
constexpr auto basicPerk = "new-horizons:battlecraft.entrench";
constexpr auto preemptivePerk = "new-horizons:battlecraft.preEmptiveStrike";

class NewHorizonsBattlecraftPreemptiveStrikeTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void acceptPerk(CGHeroInstance * hero, std::string_view perkId)
	{
		const std::string skillId(battlecraftSkill);
		const std::string requestedPerk(perkId);
		const auto rankLookup = [hero](const std::string & id)
		{
			return hero->getPerkSkillRank(id);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId != skillId
					|| offers[choice].selection.perkId != requestedPerk)
					continue;
				gameHandler->levelUpHero(hero, offers, choice, seed, false);
				ASSERT_TRUE(hero->hasActivePerk(skillId, requestedPerk));
				return;
			}
		}
		FAIL() << "The active perk was not legally offered: " << requestedPerk;
	}

	void selectPreemptiveStrike(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(battlecraftSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, basicPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, preemptivePerk);
		ASSERT_TRUE(hero->hasActivePerk(battlecraftSkill, preemptivePerk));
	}

	void removeStartingStacks()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	bool defend(CStack * stack)
	{
		battle()->activeStack = stack->unitId();
		const auto action = BattleAction::makeDefend(stack);
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), battle()->sideToPlayer(stack->unitSide()), action);
	}

	static size_t countAttacks(const RecordingGameServer & recording, uint32_t stackId,
		std::optional<bool> counter = std::nullopt)
	{
		return static_cast<size_t>(std::ranges::count_if(recording.attacks,
			[stackId, counter](const BattleAttack & attack)
		{
			return attack.stackAttacking == stackId && (!counter || attack.counter() == *counter);
		}));
	}

	static int64_t expectedPreemptiveDamage(const CBattleInfoCallback & battle,
		const CStack * defender, const CStack * attacker)
	{
		BattleAttackInfo attack(defender, attacker, 0, false);
		attack.retaliation = true;
		attack.preemptiveDamagePercent = 50;
		attack.attackerPos = defender->getPosition();
		attack.defenderPos = attacker->getPosition();
		return battle.calculateDmgRange(attack).damage.max;
	}
};
}

TEST_F(NewHorizonsBattlecraftPreemptiveStrikeTest, FirstDefendedMeleeUsesHalfDamageOnlyOncePerRoundAndPreservesRetaliation)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectPreemptiveStrike(defenderSideHero));
	startBattle();
	removeStartingStacks();
	auto * firstAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * secondAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 1000);
	auto * nextRoundAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 4), 1000);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 5), 1000);
	ASSERT_NE(firstAttacker, nullptr);
	ASSERT_NE(secondAttacker, nullptr);
	ASSERT_NE(nextRoundAttacker, nullptr);
	ASSERT_NE(defender, nullptr);
	forceMaximumDamage(defender);
	beginCombat();

	const auto currentRound = battle()->getRound();
	ASSERT_GE(currentRound, 0);
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(defender), defender, currentRound), 0);
	ASSERT_TRUE(defend(defender));
	ASSERT_TRUE(defender->defending);
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(defender), defender, currentRound), 50);
	const auto expected = expectedPreemptiveDamage(*battle(), defender, firstAttacker);
	ASSERT_GT(expected, 0);
	const auto targetCountersBefore = defender->counterAttacks.available();

	ASSERT_TRUE(attack(firstAttacker, defender->getPosition()));
	EXPECT_EQ(defender->battlecraftPreemptiveStrikeRound, currentRound);
	const auto preemptiveHit = std::ranges::find_if(server.attacks, [defender, firstAttacker](const BattleAttack & result)
	{
		return result.stackAttacking == defender->unitId() && !result.counter();
	});
	ASSERT_NE(preemptiveHit, server.attacks.end());
	const auto preemptiveDamage = std::ranges::find_if(preemptiveHit->bsa,
		[firstAttacker](const BattleStackAttacked & hit)
	{
		return hit.stackAttacked == firstAttacker->unitId();
	});
	ASSERT_NE(preemptiveDamage, preemptiveHit->bsa.end());
	EXPECT_EQ(preemptiveDamage->damageAmount, expected)
		<< "The first non-counter packet is the distinct half-damage pre-hit";
	EXPECT_EQ(countAttacks(server, defender->unitId(), false), 1u)
		<< "The pre-hit is a separate non-counter attack packet";
	EXPECT_EQ(countAttacks(server, defender->unitId(), true), 1u)
		<< "The original incoming melee still receives the defender's ordinary retaliation";
	EXPECT_EQ(defender->counterAttacks.available(), targetCountersBefore - 1)
		<< "Only the subsequent ordinary retaliation spends the normal counter";

	// Give the same stack another ordinary activation without advancing the round,
	// then Defend again. The persisted round marker, rather than Defend lifetime,
	// prevents a second pre-emptive response.
	battle()->nextTurn(defender->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_EQ(battle()->getRound(), currentRound);
	ASSERT_FALSE(defender->defending);
	ASSERT_TRUE(defend(defender));
	ASSERT_TRUE(defender->defending);
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(defender), defender, currentRound), 0);
	const auto preemptivesBeforeSecond = countAttacks(server, defender->unitId(), false);
	ASSERT_TRUE(attack(secondAttacker, defender->getPosition()));
	EXPECT_EQ(countAttacks(server, defender->unitId(), false), preemptivesBeforeSecond)
		<< "Re-Defending in the same round does not re-arm the once-per-round strike";

	endRound();
	ASSERT_GT(battle()->getRound(), currentRound);
	const auto nextRound = battle()->getRound();
	ASSERT_EQ(defender->battlecraftPreemptiveStrikeRound, currentRound);
	ASSERT_TRUE(defend(defender));
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(defender), defender, nextRound), 50);
	const auto preemptivesBeforeNextRound = countAttacks(server, defender->unitId(), false);
	ASSERT_TRUE(attack(nextRoundAttacker, defender->getPosition()));
	EXPECT_EQ(countAttacks(server, defender->unitId(), false), preemptivesBeforeNextRound + 1)
		<< "The marker from the prior round does not suppress the next round's first eligible hit";
	EXPECT_EQ(defender->battlecraftPreemptiveStrikeRound, nextRound);
}

TEST_F(NewHorizonsBattlecraftPreemptiveStrikeTest, NoPerkOrNoDefendDoesNotTriggerThePreemptiveStrike)
{
	startGame();
	startBattle();
	removeStartingStacks();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 5), 100);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	forceMaximumDamage(defender);
	blockRetaliation(attacker);
	beginCombat();
	ASSERT_TRUE(defend(defender));
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(defender), defender, battle()->getRound()), 0);
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(countAttacks(server, defender->unitId(), false), 0u);
	EXPECT_EQ(defender->battlecraftPreemptiveStrikeRound, -1);
}

TEST_F(NewHorizonsBattlecraftPreemptiveStrikeTest, ActivePerkIgnoresMeleeAgainstNonDefendingStacksAndRangedShots)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectPreemptiveStrike(defenderSideHero));
	startBattle();
	removeStartingStacks();
	auto * meleeAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 100);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(2, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 5), 100);
	ASSERT_NE(meleeAttacker, nullptr);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defender, nullptr);
	blockRetaliation(defender);
	beginCombat();
	ASSERT_FALSE(defender->defended());
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(defender), defender, battle()->getRound()), 0);
	ASSERT_TRUE(attack(meleeAttacker, defender->getPosition()));
	EXPECT_EQ(countAttacks(server, defender->unitId(), false), 0u);
	EXPECT_EQ(defender->battlecraftPreemptiveStrikeRound, -1);

	ASSERT_TRUE(defend(defender));
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(defender), defender, battle()->getRound()), 50);
	battle()->activeStack = shooter->unitId();
	const auto shot = BattleAction::makeShotAttack(shooter, defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), battle()->sideToPlayer(shooter->unitSide()), shot));
	EXPECT_EQ(countAttacks(server, defender->unitId(), false), 0u)
		<< "The reaction is restricted to incoming melee, not a ranged attack";
	EXPECT_EQ(defender->battlecraftPreemptiveStrikeRound, -1);
}

TEST_F(NewHorizonsBattlecraftPreemptiveStrikeTest, OrdinaryMeleeMagogCanTriggerAndTimeStoppedDefenderCannot)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectPreemptiveStrike(defenderSideHero));
	startBattle();
	removeStartingStacks();
	auto * magog = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(5, 5), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 5), 1000);
	auto * timeStoppedDefender = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(9, 5), 10);
	auto * disabledDefender = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(11, 5), 10);
	ASSERT_NE(magog, nullptr);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(timeStoppedDefender, nullptr);
	ASSERT_NE(disabledDefender, nullptr);
	blockRetaliation(magog);
	beginCombat();
	ASSERT_TRUE(defend(defender));
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(defender), defender, battle()->getRound()), 50);
	ASSERT_TRUE(magog->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	ASSERT_TRUE(attack(magog, defender->getPosition()));
	EXPECT_EQ(countAttacks(server, defender->unitId(), false), 1u)
		<< "A Magog's melee blow is physical and still receives the reaction";

	ASSERT_TRUE(defend(timeStoppedDefender));
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(timeStoppedDefender), timeStoppedDefender, battle()->getRound()), 50);
	auto timeStop = Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	battle()->addOrUpdateUnitBonus(timeStoppedDefender, timeStop, true);
	ASSERT_TRUE(timeStoppedDefender->isTimeStopped());
	EXPECT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(timeStoppedDefender), timeStoppedDefender, battle()->getRound()), 0)
		<< "Time Stopped creatures cannot receive a defensive reaction";

	ASSERT_TRUE(defend(disabledDefender));
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(disabledDefender), disabledDefender, battle()->getRound()), 50);
	auto disabled = Bonus(BonusDuration::ONE_BATTLE, BonusType::NOT_ACTIVE,
		BonusSource::OTHER, 1, BonusSourceID());
	battle()->addOrUpdateUnitBonus(disabledDefender, disabled, true);
	ASSERT_FALSE(disabledDefender->canMove());
	EXPECT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		battle()->battleGetOwnerHero(disabledDefender), disabledDefender, battle()->getRound()), 0)
		<< "A disabled stack cannot deliver the reaction before an attack clears its status";
}

TEST_F(NewHorizonsBattlecraftPreemptiveStrikeTest, RoundMarkerCopiesDefaultsAndUsesVersionedUnitUpdateGuards)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 5), 10);
	ASSERT_NE(stack, nullptr);
	ASSERT_EQ(stack->battlecraftPreemptiveStrikeRound, -1);
	auto state = stack->acquireState();
	EXPECT_EQ(state->battlecraftPreemptiveStrikeRound, -1)
		<< "Legacy/default state has no active round marker";
	state->battlecraftPreemptiveStrikeRound = 3;
	auto copy = stack->acquireState();
	*copy = *state;
	EXPECT_EQ(copy->battlecraftPreemptiveStrikeRound, 3);
	const auto serialized = state->save();
	copy->load(serialized);
	EXPECT_EQ(copy->battlecraftPreemptiveStrikeRound, 3);
	auto legacy = serialized;
	legacy["state"].Struct().erase("battlecraftPreemptiveStrikeRound");
	copy->load(legacy);
	EXPECT_EQ(copy->battlecraftPreemptiveStrikeRound, -1);
	auto invalid = serialized;
	invalid["state"]["battlecraftPreemptiveStrikeRound"] = JsonNode(-2);
	EXPECT_THROW(copy->load(invalid), std::runtime_error);

	state->battlecraftPreemptiveStrikeRound = 3;
	UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & update;
	current.iser.cb = gameState().get();
	UnitChanges decoded;
	current.iser & decoded;
	EXPECT_EQ(decoded.data["state"]["battlecraftPreemptiveStrikeRound"].Integer(), 3);

	CMemorySerializer oldUnitWriter;
	oldUnitWriter.oser.version = ESerializationVersion::NEW_HORIZONS_MANDATE_OF_HEAVEN;
	EXPECT_THROW(oldUnitWriter.oser & update, std::runtime_error);
	EXPECT_TRUE(oldUnitWriter.extractBuffer().empty());

	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.push_back(update);
	CMemorySerializer oldPacketWriter;
	oldPacketWriter.oser.version = ESerializationVersion::NEW_HORIZONS_MANDATE_OF_HEAVEN;
	EXPECT_THROW(oldPacketWriter.oser & packet, std::runtime_error);
	EXPECT_TRUE(oldPacketWriter.extractBuffer().empty())
		<< "The enclosing packet rejects unsupported marker state before writing its header";
}
