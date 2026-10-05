/*
 * NewHorizonsBattlecraftPreemptiveStrikeAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"

#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/CStack.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
constexpr auto battlecraftSkill = "new-horizons:battlecraft";
constexpr auto basicPerk = "new-horizons:battlecraft.entrench";
constexpr auto preemptivePerk = "new-horizons:battlecraft.preEmptiveStrike";

class PreemptiveAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit PreemptiveAIEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class PreemptiveAICallback final : public CBattleCallback
{
public:
	explicit PreemptiveAICallback(PlayerColor player)
		: CBattleCallback(player, nullptr)
	{}

	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
};

class NewHorizonsBattlecraftPreemptiveStrikeAITest : public BattleTestFixture
{
protected:
	std::shared_ptr<PreemptiveAIEnvironment> environment;
	std::shared_ptr<PreemptiveAICallback> callback;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
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

	void removeOtherStacks(std::initializer_list<CStack *> kept)
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(!std::ranges::any_of(kept, [unit](const CStack * retained)
				{ return retained && retained->unitId() == unit->unitId(); }))
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	bool defend(CStack * stack)
	{
		battle()->activeStack = stack->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), BattleAction::makeDefend(stack));
	}

	void initializeAI(PlayerColor player)
	{
		callback = std::make_shared<PreemptiveAICallback>(player);
		callback->onBattleStarted(battle());
		environment = std::make_shared<PreemptiveAIEnvironment>(gameState());
	}

	std::shared_ptr<HypotheticBattle> simulation() const
	{
		return std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	}

	BattleAction choose(CStack * stack)
	{
		const auto player = battle()->sideToPlayer(stack->unitSide());
		BattleEvaluator evaluator(environment, callback, stack, player, BattleID(0),
			stack->unitSide(), 1.0f, 2);
		return evaluator.selectStackAction(stack);
	}
};
}

TEST_F(NewHorizonsBattlecraftPreemptiveStrikeAITest, ForecastConsumesTheDefendMarkerOnlyInTheDetachedExchange)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectPreemptiveStrike(defenderSideHero));
	startBattle();
	beginCombat();
	auto * firstAttacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * secondAttacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(7, 5), 1000);
	auto * defender = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(6, 5), 1000);
	ASSERT_NE(firstAttacker, nullptr);
	ASSERT_NE(secondAttacker, nullptr);
	ASSERT_NE(defender, nullptr);
	removeOtherStacks({firstAttacker, secondAttacker, defender});
	forceMaximumDamage(defender);
	blockRetaliation(defender);
	blockRetaliation(firstAttacker);
	blockRetaliation(secondAttacker);
	ASSERT_TRUE(defend(defender));
	ASSERT_EQ(defender->battlecraftPreemptiveStrikeRound, -1);
	battle()->activeStack = firstAttacker->unitId();
	initializeAI(PlayerColor(1));

	const auto firstHealth = firstAttacker->getAvailableHealth();
	const auto defenderHealth = defender->getAvailableHealth();
	auto model = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedFirst = model->battleGetUnitByID(firstAttacker->unitId());
	const auto * projectedDefender = model->battleGetUnitByID(defender->unitId());
	ASSERT_NE(projectedFirst, nullptr);
	ASSERT_NE(projectedDefender, nullptr);
	const BattleAttackInfo incoming(projectedFirst, projectedDefender, 0, false);
	BattleAttackInfo expectedReaction(projectedDefender, projectedFirst, 0, false);
	expectedReaction.retaliation = true;
	expectedReaction.preemptiveDamagePercent = 50;
	const auto expectedDamage = std::min(firstHealth, model->battleExpectedLuckDamage(expectedReaction));
	ASSERT_GT(expectedDamage, 0);

	const auto possibility = AttackPossibility::evaluate(incoming,
		projectedFirst->getPosition(), damageCache, model);
	ASSERT_NE(possibility.effectPreview, nullptr);
	ASSERT_NE(possibility.attackerState, nullptr);
	EXPECT_EQ(possibility.attackerState->getAvailableHealth(), firstHealth - expectedDamage)
		<< "The detached pre-hit is forecast before the incoming primary melee damage";
	EXPECT_EQ(possibility.effectPreview->getForUpdate(defender->unitId())->battlecraftPreemptiveStrikeRound,
		battle()->getRound());
	EXPECT_EQ(defender->battlecraftPreemptiveStrikeRound, -1)
		<< "Evaluating an attack must not mutate the live battle unit";
	EXPECT_EQ(firstAttacker->getAvailableHealth(), firstHealth);
	EXPECT_EQ(defender->getAvailableHealth(), defenderHealth);

	BattleExchangeVariant exchange;
	exchange.trackAttack(possibility, model, damageCache);
	EXPECT_EQ(model->getForUpdate(defender->unitId())->battlecraftPreemptiveStrikeRound,
		battle()->getRound())
		<< "Accepted exchange replay carries the consumed round marker to later projections";
	EXPECT_EQ(defender->battlecraftPreemptiveStrikeRound, -1);

	DamageCache secondDamageCache;
	secondDamageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedSecond = model->battleGetUnitByID(secondAttacker->unitId());
	const auto * projectedTarget = model->battleGetUnitByID(defender->unitId());
	const auto second = AttackPossibility::evaluate(BattleAttackInfo(projectedSecond, projectedTarget, 0, false),
		projectedSecond->getPosition(), secondDamageCache, model);
	EXPECT_NEAR(second.attackerDamageReduce, 0.0f, 0.001f)
		<< "A second incoming melee in the same round cannot receive the pre-hit again";
}

TEST_F(NewHorizonsBattlecraftPreemptiveStrikeAITest, ActivePerkCanMakeAnImmobileStackChooseDefendWithoutMutatingIt)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectPreemptiveStrike(attackerSideHero));
	startBattle();
	beginCombat();
	auto * activeStack = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	auto * threat = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(8, 5), 5);
	ASSERT_NE(activeStack, nullptr);
	ASSERT_NE(threat, nullptr);
	removeOtherStacks({activeStack, threat});
	const Bonus immobilized(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -activeStack->getMovementRange(0), BonusSourceID());
	activeStack->addNewBonus(std::make_shared<Bonus>(immobilized));
	ASSERT_EQ(activeStack->getMovementRange(0), 0);
	battle()->activeStack = activeStack->unitId();
	initializeAI(PlayerColor(0));

	const auto action = choose(activeStack);
	EXPECT_EQ(action.actionType, EActionType::DEFEND)
		<< "When its only credible use is defense, AI should account for the projected pre-hit threat";
	EXPECT_FALSE(activeStack->defended())
		<< "AI valuation does not set Defend on the live unit";
	EXPECT_EQ(activeStack->battlecraftPreemptiveStrikeRound, -1);
	EXPECT_FALSE(threat->defended());
}
