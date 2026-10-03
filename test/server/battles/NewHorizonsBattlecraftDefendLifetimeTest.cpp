/*
 * NewHorizonsBattlecraftDefendLifetimeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>

namespace
{
class BattlecraftDefendLifetimeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit BattlecraftDefendLifetimeEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsBattlecraftDefendLifetimeTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	SecondarySkill battlecraft() const
	{
		const int decoded = SecondarySkill::decode("new-horizons:battlecraft");
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void setBattlecraftRank(CGHeroInstance * hero, int rank)
	{
		hero->setSecSkillLevel(battlecraft(), rank, ChangeValueMode::ABSOLUTE);
	}

	void selectBasicCommand(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode("new-horizons:command");
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	}

	void removeStartingUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	bool act(const battle::Unit * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), battle()->battleGetOwner(stack), action);
	}

	void advanceUntilActive(const CStack * expected)
	{
		for(int attempt = 0; attempt < 24; ++attempt)
		{
			if(battle()->battleActiveUnit() == expected)
				return;
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(act(active, BattleAction::makeDefend(active)));
		}
		FAIL() << "The expected stack did not receive an ordinary activation";
	}

	std::size_t queueActivations(const CStack * stack) const
	{
		return static_cast<std::size_t>(std::count_if(server.stackActivations.begin(), server.stackActivations.end(),
			[stack](const BattleSetActiveStack & activation)
			{
				return activation.stack == stack->unitId()
					&& activation.reason == BattleUnitTurnReason::TURN_QUEUE;
			}));
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}
};
}

TEST_F(NewHorizonsBattlecraftDefendLifetimeTest, AcceptedDefendSurvivesRolloverUntilItsNextRealQueueActivation)
{
	startGame();
	setBattlecraftRank(attackerSideHero, MasteryLevel::EXPERT);
	startBattle();
	removeStartingUnits();
	auto * defender = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(4, 5), 10);
	auto * damageProbe = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(1, 5), 10);
	auto * striker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	auto * slowReserve = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(14, 5), 10);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(damageProbe, nullptr);
	ASSERT_NE(striker, nullptr);
	ASSERT_NE(slowReserve, nullptr);
	forceMaximumDamage(damageProbe);
	beginCombat();

	const BattleAttackInfo probeAttack(damageProbe, defender, 0, false);
	const auto unguardedDamage = battle()->calculateDmgRange(probeAttack).damage.max;
	ASSERT_GT(unguardedDamage, 0);
	advanceUntilActive(defender);
	ASSERT_TRUE(act(defender, BattleAction::makeDefend(defender)));
	ASSERT_TRUE(defender->defending);
	ASSERT_TRUE(defender->hasBonusOfType(BonusType::UNIT_DEFENDING));
	ASSERT_GT(defender->defensiveStanceMeleeBonus, 0);
	const auto capturedMeleeStance = defender->defensiveStanceMeleeBonus;
	const auto defendedDamage = battle()->calculateDmgRange(probeAttack).damage.max;
	EXPECT_LT(defendedDamage, unguardedDamage);

	const auto retaliationTotal = defender->counterAttacks.total();
	ASSERT_GT(retaliationTotal, 0);
	advanceUntilActive(striker);
	ASSERT_TRUE(attack(striker, defender->getPosition()));
	ASSERT_EQ(battle()->getRound(), 1)
		<< "The untouched slower reserve must leave the spent-counter observation inside round one";
	ASSERT_EQ(defender->counterAttacks.available(), retaliationTotal - 1)
		<< "The physical attack should spend the defender's normal retaliation allowance";

	endRound();
	ASSERT_EQ(battle()->getRound(), 2);
	ASSERT_NE(battle()->battleActiveUnit(), defender)
		<< "A faster stack should open the next round before the defended stack's normal turn";
	EXPECT_FALSE(defender->defending) << "The action flag resets at round rollover so the stack can move";
	EXPECT_TRUE(defender->defended()) << "The captured defensive stance persists until the next activation";
	EXPECT_TRUE(defender->hasBonusOfType(BonusType::UNIT_DEFENDING));
	EXPECT_TRUE(defender->willMove(0));
	EXPECT_EQ(defender->defensiveStanceMeleeBonus, capturedMeleeStance);
	EXPECT_EQ(battle()->calculateDmgRange(probeAttack).damage.max, defendedDamage);
	EXPECT_EQ(defender->counterAttacks.total(), retaliationTotal);
	EXPECT_EQ(defender->counterAttacks.available(), retaliationTotal)
		<< "Retaliation counters still reset normally at the round boundary";

	BattlecraftDefendLifetimeEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto branchDefender = branch->getForUpdate(defender->unitId());
	auto parentDefender = parent->getForUpdate(defender->unitId());
	auto siblingDefender = sibling->getForUpdate(defender->unitId());
	ASSERT_NE(branchDefender, nullptr);
	ASSERT_NE(parentDefender, nullptr);
	ASSERT_NE(siblingDefender, nullptr);
	ASSERT_TRUE(branchDefender->defended());
	EXPECT_EQ(branchDefender->defensiveStanceMeleeBonus, capturedMeleeStance);
	EXPECT_TRUE(parentDefender->defended());
	EXPECT_TRUE(siblingDefender->defended());

	branch->nextTurn(defender->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(branch->getForUpdate(defender->unitId())->defended());
	EXPECT_FALSE(branch->getForUpdate(defender->unitId())->hasBonusOfType(BonusType::UNIT_DEFENDING));
	EXPECT_EQ(branch->getForUpdate(defender->unitId())->defensiveStanceMeleeBonus, 0);
	EXPECT_TRUE(parent->getForUpdate(defender->unitId())->defended());
	EXPECT_TRUE(sibling->getForUpdate(defender->unitId())->defended());
	EXPECT_TRUE(defender->defended()) << "A detached activation cannot mutate the live battle";

	const auto queuedBefore = queueActivations(defender);
	advanceUntilActive(defender);
	EXPECT_EQ(queueActivations(defender), queuedBefore + 1);
	EXPECT_FALSE(defender->defended());
	EXPECT_FALSE(defender->hasBonusOfType(BonusType::UNIT_DEFENDING));
	EXPECT_EQ(defender->defensiveStanceMeleeBonus, 0);
	EXPECT_EQ(battle()->calculateDmgRange(probeAttack).damage.max, unguardedDamage);
}

TEST_F(NewHorizonsBattlecraftDefendLifetimeTest, PriorRoundDefendDoesNotQualifySecondWindButCurrentRoundDefendDoes)
{
	startGame();
	selectBasicCommand(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * fastAlly = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(1, 5), 10);
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(4, 5), 10);
	auto * commandWindow = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(4, 8), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(fastAlly, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(commandWindow, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();
	advanceUntilActive(target);
	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));
	ASSERT_TRUE(target->defended());
	endRound();
	ASSERT_EQ(battle()->getRound(), 2);
	ASSERT_EQ(battle()->battleActiveUnit(), fastAlly);

	EXPECT_TRUE(target->hasBonusOfType(BonusType::UNIT_DEFENDING));
	EXPECT_FALSE(target->defending);
	EXPECT_FALSE(target->moved());
	EXPECT_FALSE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND))
		<< "A prior-round stance must not count as this round's spent activation";
	EXPECT_FALSE(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
		{target->unitId()}));

	advanceUntilActive(target);
	EXPECT_FALSE(target->hasBonusOfType(BonusType::UNIT_DEFENDING))
		<< "The old stance expires when the stack receives its next real queue activation";
	EXPECT_FALSE(target->defending);
	EXPECT_FALSE(target->moved());
	EXPECT_FALSE(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
		{target->unitId()}));

	ASSERT_TRUE(act(target, BattleAction::makeDefend(target)));
	EXPECT_TRUE(target->defending);
	advanceUntilActive(commandWindow);
	EXPECT_TRUE(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
		{target->unitId()}))
		<< "A new Defend in this round is a valid spent activation for Second Wind";
	EXPECT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND));
}

TEST_F(NewHorizonsBattlecraftDefendLifetimeTest, AcceptedWaitStateRemainsRoundScoped)
{
	startGame();
	startBattle();
	removeStartingUnits();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(4, 5), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(15, 5), 10);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();

	const auto * activeUnit = battle()->battleActiveUnit();
	ASSERT_NE(activeUnit, nullptr);
	auto * active = battle()->getStack(activeUnit->unitId());
	ASSERT_NE(active, nullptr);
	ASSERT_TRUE(act(active, BattleAction::makeWait(active)));
	ASSERT_TRUE(active->waiting);
	ASSERT_TRUE(active->waitedThisTurn);

	advanceRound();
	EXPECT_EQ(battle()->getRound(), 2);
	EXPECT_FALSE(active->waiting);
	EXPECT_FALSE(active->waitedThisTurn);
	EXPECT_FALSE(active->battlecraftWaitBonusUsed);
}
