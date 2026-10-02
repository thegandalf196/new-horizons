/*
 * NewHorizonsNoQuarterAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"

namespace
{
class NoQuarterEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit NoQuarterEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsNoQuarterAITest : public BattleTestFixture
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
		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto & offensePerks = perkRules["skills"][newHorizonsOffense::SKILL]["perks"].Vector();
		const auto noQuarter = std::find_if(offensePerks.begin(), offensePerks.end(), [](const auto & perk)
		{
			return perk["id"].String() == newHorizonsOffense::NO_QUARTER;
		});
		if(noQuarter == offensePerks.end())
			throw std::runtime_error("No Quarter is missing from the New Horizons perk registry");
		(*noQuarter)["effect"]["status"].String() = "active";
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void startBattleWithNoQuarter()
	{
		startGame();
		const int decodedOffense = SecondarySkill::decode(newHorizonsOffense::SKILL);
		ASSERT_GE(decodedOffense, 0);
		const SecondarySkill offense(decodedOffense);
		attackerSideHero->setSecSkillLevel(offense, MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, "new-horizons:offense.shockAssault"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(
			newHorizonsOffense::SKILL, "new-horizons:offense.shockAssault"));
		attackerSideHero->setSecSkillLevel(offense, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(
			newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE));
		attackerSideHero->setSecSkillLevel(offense, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::NO_QUARTER});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(
			newHorizonsOffense::SKILL, newHorizonsOffense::NO_QUARTER));
		startBattle();
	}

	void setAvailableHealth(CStack * stack, int64_t desiredHealth)
	{
		auto state = stack->acquireState();
		ASSERT_GE(desiredHealth, 0);
		ASSERT_LE(desiredHealth, state->getAvailableHealth());
		int64_t damage = state->getAvailableHealth() - desiredHealth;
		state->damage(damage);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -damage;
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	bool hasNoQuarterMorale(const battle::Unit * stack) const
	{
		const auto bonuses = stack->getAllBonuses(CSelector([](const Bonus * bonus)
		{
			return newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
		}));
		return bonuses && !bonuses->empty();
	}
};
}

TEST_F(NewHorizonsNoQuarterAITest, ProjectedHitSuppressesRetaliationAndReplaysWithoutMutatingLiveBattle)
{
	startBattleWithNoQuarter();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::UNLIMITED_RETALIATIONS, BonusSource::OTHER, 1, BonusSourceID()));
	forceMaximumDamage(attacker);
	beginCombat();
	battle()->activeStack = attacker->unitId();

	const int64_t quarter = defender->getTotalHealth() / 4;
	const int64_t predictedDamage = battle()->calculateDmgRange(
		BattleAttackInfo(attacker, defender, 0, false)).damage.max;
	ASSERT_GT(predictedDamage, 0);
	setAvailableHealth(defender, quarter + predictedDamage - 1);
	const int64_t liveHealth = defender->getAvailableHealth();

	NoQuarterEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	auto projectedAttacker = model->getForUpdate(attacker->unitId());
	auto projectedDefender = model->getForUpdate(defender->unitId());
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(
		projectedAttacker.get(), projectedDefender.get(), 0, false), attacker->getPosition(), cache, model);

	ASSERT_NE(prediction.effectPreview, nullptr);
	ASSERT_FALSE(prediction.defenderDead);
	ASSERT_EQ(prediction.fortuneStrikes.size(), 1u);
	EXPECT_FALSE(prediction.fortuneStrikes.front().retaliation);
	EXPECT_EQ(prediction.fortuneStrikes.front().noQuarterTargets.size(), 1u);
	EXPECT_EQ(prediction.fortuneStrikes.front().noQuarterTargets.front().first, defender->unitId());
	EXPECT_EQ(prediction.fortuneStrikes.front().noQuarterTargets.front().second, 1);
	EXPECT_NEAR(prediction.attackerDamageReduce, 0.0f, 0.001f);

	const auto * projectedTarget = prediction.effectPreview->battleGetUnitByID(defender->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	EXPECT_TRUE(newHorizonsOffense::belowNoQuarterThreshold(
		projectedTarget->getAvailableHealth(), projectedTarget->getTotalHealth()));
	EXPECT_FALSE(prediction.effectPreview->getForUpdate(defender->unitId())->counterAttacks.canUse());
	EXPECT_EQ(prediction.effectPreview->getForUpdate(defender->unitId())->noQuarterMoraleActivationsRemaining, 1);
	EXPECT_TRUE(hasNoQuarterMorale(projectedTarget));

	EXPECT_EQ(defender->getAvailableHealth(), liveHealth);
	EXPECT_EQ(defender->noQuarterMoraleActivationsRemaining, 0);
	EXPECT_FALSE(defender->hasBonusOfType(BonusType::NO_RETALIATION));
	EXPECT_TRUE(defender->counterAttacks.canUse());
	EXPECT_FALSE(hasNoQuarterMorale(defender));

	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, cache);
	auto replayedTarget = model->getForUpdate(defender->unitId());
	EXPECT_TRUE(newHorizonsOffense::belowNoQuarterThreshold(
		replayedTarget->getAvailableHealth(), replayedTarget->getTotalHealth()));
	EXPECT_FALSE(replayedTarget->counterAttacks.canUse());
	EXPECT_EQ(replayedTarget->noQuarterMoraleActivationsRemaining, 1);
	EXPECT_TRUE(hasNoQuarterMorale(replayedTarget.get()));
	EXPECT_EQ(defender->getAvailableHealth(), liveHealth);
	EXPECT_EQ(defender->noQuarterMoraleActivationsRemaining, 0);
	EXPECT_TRUE(defender->counterAttacks.canUse());

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(defender->noQuarterMoraleActivationsRemaining, 1);
	EXPECT_TRUE(hasNoQuarterMorale(defender));
	EXPECT_FALSE(defender->counterAttacks.canUse());
}

TEST_F(NewHorizonsNoQuarterAITest, EvaluateOnlyPreservesMoraleLifetimeAndCommittedActionConsumesIt)
{
	startBattleWithNoQuarter();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 10);
	beginCombat();
	battle()->activeStack = attacker->unitId();

	NoQuarterEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto projectedAttacker = model->getForUpdate(attacker->unitId());
	projectedAttacker->applyNoQuarter(1);
	ASSERT_EQ(projectedAttacker->noQuarterMoraleActivationsRemaining, 1);
	ASSERT_TRUE(hasNoQuarterMorale(projectedAttacker.get()));

	DamageCache cache;
	BattleExchangeVariant exchange;
	exchange.trackAttack(projectedAttacker, model->getForUpdate(defender->unitId()), false,
		true, cache, model, true, false);
	EXPECT_EQ(projectedAttacker->noQuarterMoraleActivationsRemaining, 1);
	EXPECT_TRUE(hasNoQuarterMorale(projectedAttacker.get()));

	exchange.trackAttack(projectedAttacker, model->getForUpdate(defender->unitId()), false,
		true, cache, model, false, false);
	EXPECT_EQ(projectedAttacker->noQuarterMoraleActivationsRemaining, 0);
	EXPECT_FALSE(hasNoQuarterMorale(projectedAttacker.get()));
	EXPECT_EQ(attacker->noQuarterMoraleActivationsRemaining, 0);
	EXPECT_FALSE(hasNoQuarterMorale(attacker));
}

TEST_F(NewHorizonsNoQuarterAITest, TimeStoppedProjectedTargetDoesNotReceiveNoQuarter)
{
	startBattleWithNoQuarter();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * stopped = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	forceMaximumDamage(attacker);
	beginCombat();
	BattleAttackInfo attackInfo(attacker, stopped, 0, false);
	const int64_t hitDamage = battle()->calculateDmgRange(attackInfo).damage.max;
	ASSERT_GT(hitDamage, 0);
	setAvailableHealth(stopped, stopped->getTotalHealth() / 4 + hitDamage - 1);
	const Bonus timeStop(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	battle()->addOrUpdateUnitBonus(stopped, timeStop, true);
	ASSERT_TRUE(stopped->isTimeStopped());
	battle()->activeStack = attacker->unitId();

	NoQuarterEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(
		model->getForUpdate(attacker->unitId()).get(), model->getForUpdate(stopped->unitId()).get(), 0, false),
		attacker->getPosition(), cache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	const auto projectedTarget = prediction.effectPreview->getForUpdate(stopped->unitId());
	EXPECT_TRUE(projectedTarget->isTimeStopped());
	EXPECT_EQ(projectedTarget->noQuarterMoraleActivationsRemaining, 0);
	EXPECT_FALSE(projectedTarget->hasBonusOfType(BonusType::NO_RETALIATION));
	EXPECT_FALSE(hasNoQuarterMorale(projectedTarget.get()));
	EXPECT_EQ(stopped->noQuarterMoraleActivationsRemaining, 0);
	EXPECT_FALSE(stopped->hasBonusOfType(BonusType::NO_RETALIATION));
}
