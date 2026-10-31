/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsMaintenanceFixture.h"
#include "../../AI/BattleAI/DefendEvaluation.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsMaintenanceSpecialties.h"
#include "ProjectedTargetHitFixture.h"

namespace
{
class MaintenanceEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MaintenanceEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsMaintenanceAITest : public NewHorizonsMaintenanceFixture
{
protected:
	std::shared_ptr<MaintenanceEnvironment> environment;
	std::vector<std::byte> rngSnapshot()
	{
		auto bytes = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto other = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		bytes.insert(bytes.end(), other.begin(), other.end());
		return bytes;
	}
	std::shared_ptr<HypotheticBattle> model()
	{
		environment = std::make_shared<MaintenanceEnvironment>(gameState());
		return std::make_shared<HypotheticBattle>(environment.get(),
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	}
	void expectRepairFallback(int mode, EActionType expected)
	{
		ASSERT_NO_FATAL_FAILURE(prepare("core:josephine", mode));
		int64_t damageAmount = actor->getAvailableHealth() - actor->getMaxHealth() + 14;
		actor->damage(damageAmount);
		for(auto * unit : {actor, victim})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER, -100, BonusSourceID()));
		environment = std::make_shared<MaintenanceEnvironment>(gameState());
		auto callback = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
		callback->onBattleStarted(battle());
		const auto live = gameState()->saveToMemory();
		const auto rng = rngSnapshot();
		BattleEvaluator evaluator(environment, callback, actor, PlayerColor(0),
			BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
		const auto action = evaluator.selectStackAction(actor);
		EXPECT_EQ(action.actionType, expected);
		EXPECT_EQ(action.stackNumber, actor->unitId());
		EXPECT_EQ(gameState()->saveToMemory(), live);
		EXPECT_EQ(rngSnapshot(), rng);
		RecordProperty("selectedAction", static_cast<int>(action.actionType));
	}
};

TEST_F(NewHorizonsMaintenanceAITest, JosephineRepairForecastUsesTopWoundAndKeepsParentAndLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:josephine"));
	auto parent = model();
	auto wounded = parent->getForUpdate(actor->unitId());
	int64_t damageAmount = 14;
	wounded->damage(damageAmount);
	const auto health = wounded->getAvailableHealth();
	const auto count = wounded->getCount();
	ASSERT_TRUE(defendEvaluation::capabilities(*parent, *wounded).maintenance);
	const auto live = gameState()->saveToMemory();
	const auto rng = rngSnapshot();
	auto preview = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto target = preview->getForUpdate(actor->unitId());
	target->defending = true;
	const auto expected = newHorizonsMaintenanceSpecialties::woundHealing(*preview, *target,
		newHorizonsMaintenanceSpecialties::Trigger::VOLUNTARY_DEFEND);
	ASSERT_GT(expected, 0);
	EXPECT_EQ(defendEvaluation::projectWoundHealing(*preview, *target), expected);
	EXPECT_EQ(target->getAvailableHealth(), health + expected);
	EXPECT_EQ(target->getCount(), count);
	EXPECT_EQ(parent->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(gameState()->saveToMemory(), live);
	EXPECT_EQ(rngSnapshot(), rng);
}

TEST_F(NewHorizonsMaintenanceAITest, GerwulfActivationRepairDoesNotHealRejectedOrDeferredWaitWindows)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gerwulf"));
	auto parent = model();
	auto wounded = parent->getForUpdate(actor->unitId());
	int64_t damageAmount = 20;
	wounded->damage(damageAmount);
	const auto health = wounded->getAvailableHealth();
	const auto count = wounded->getCount();
	auto rejected = std::make_shared<HypotheticBattle>(environment.get(), parent);
	rejected->nextTurn(actor->unitId(), BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_EQ(rejected->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	auto waited = std::make_shared<HypotheticBattle>(environment.get(), parent);
	waited->makeWait(waited->battleGetUnitByID(actor->unitId()));
	waited->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(waited->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	auto automatic = std::make_shared<HypotheticBattle>(environment.get(), parent);
	automatic->makeWait(automatic->battleGetUnitByID(actor->unitId()));
	automatic->nextTurn(actor->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION);
	EXPECT_EQ(automatic->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	auto active = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto expected = newHorizonsMaintenanceSpecialties::activationHealing(*active,
		*active->battleGetUnitByID(actor->unitId()), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_GT(expected, 0);
	active->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(active->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health + expected);
	EXPECT_EQ(active->battleGetUnitByID(actor->unitId())->getCount(), count);
	EXPECT_EQ(parent->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
}

TEST_F(NewHorizonsMaintenanceAITest, KrellionOnlyValuesAdditionalLegalAdjacentCounterCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:krellion"));
	auto parent = model();
	auto ogre = parent->getForUpdate(actor->unitId());
	auto enemy = parent->getForUpdate(victim->unitId());
	int64_t damageAmount = enemy->getAvailableHealth() - enemy->getMaxHealth();
	enemy->damage(damageAmount);
	enemy->setPosition(BattleHex(4, 5));
	ogre->counterAttacks.use(1);
	ASSERT_EQ(ogre->counterAttacks.available(), 0);
	ASSERT_TRUE(defendEvaluation::capabilities(*parent, *ogre).maintenance);
	auto preview = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto projected = preview->getForUpdate(actor->unitId());
	projected->defending = true;
	ASSERT_GT(projected->counterAttacks.available(), 0);
	EXPECT_GT(defendEvaluation::extraRetaliationValue(*parent, preview, *ogre), 0.0f);
	projected->defending = false;
	EXPECT_FLOAT_EQ(defendEvaluation::extraRetaliationValue(*parent, preview, *ogre), 0.0f);
	projected->defending = true;
	projected->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::NO_RETALIATION, BonusSource::OTHER, 1, BonusSourceID())});
	EXPECT_FLOAT_EQ(defendEvaluation::extraRetaliationValue(*parent, preview, *ogre), 0.0f);
	EXPECT_FALSE(parent->battleGetUnitByID(actor->unitId())->defended());
	EXPECT_EQ(parent->battleGetUnitByID(actor->unitId())->acquireState()->counterAttacks.available(), 0);
}

TEST_F(NewHorizonsMaintenanceAITest, MissingCapturedRepairKeyKeepsLegacyProjectionUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:josephine", 1));
	auto parent = model();
	auto target = parent->getForUpdate(actor->unitId());
	int64_t damageAmount = 14;
	target->damage(damageAmount);
	const auto health = target->getAvailableHealth();
	EXPECT_FALSE(defendEvaluation::capabilities(*parent, *target).maintenance);
	target->defending = true;
	EXPECT_EQ(defendEvaluation::projectWoundHealing(*parent, *target), 0);
	EXPECT_EQ(target->getAvailableHealth(), health);
}

TEST_F(NewHorizonsMaintenanceAITest, JosephineMayChooseUsefulRepairInsteadOfWaiting)
{
	ASSERT_NO_FATAL_FAILURE(expectRepairFallback(0, EActionType::DEFEND));
}

TEST_F(NewHorizonsMaintenanceAITest, MissingRepairCaptureKeepsOrdinaryWaitChoice)
{
	ASSERT_NO_FATAL_FAILURE(expectRepairFallback(1, EActionType::WAIT));
}

TEST_F(NewHorizonsMaintenanceAITest, ZeroRepairBudgetKeepsOrdinaryWaitChoice)
{
	ASSERT_NO_FATAL_FAILURE(expectRepairFallback(2, EActionType::WAIT));
}
