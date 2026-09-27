/*
 * NewHorizonsPaviseAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"

namespace
{
class PaviseEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit PaviseEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class PaviseCallback final : public CBattleCallback
{
public:
	PaviseCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
};

class NewHorizonsPaviseAITest : public BattleTestFixture
{
protected:
	std::shared_ptr<PaviseEnvironment> environment;
	std::shared_ptr<PaviseCallback> callback;
	CStack * activeStack = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(bool pavise, bool immobilize = true)
	{
		startGame();
		if(pavise)
		{
			const auto decoded = SecondarySkill::decode("new-horizons:armorer");
			ASSERT_GE(decoded, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.pavise"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.pavise"));
		}
		startBattle();
		beginCombat();
		activeStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 20);
		auto * shooter = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(12, 5), 100);
		ASSERT_NE(activeStack, nullptr);
		ASSERT_NE(shooter, nullptr);

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != activeStack && unit != shooter)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		if(immobilize)
		{
			Bonus immobilized;
			immobilized.type = BonusType::STACKS_SPEED;
			immobilized.duration = BonusDuration::ONE_BATTLE;
			immobilized.val = -activeStack->getMovementRange();
			activeStack->addNewBonus(std::make_shared<Bonus>(immobilized));
			ASSERT_EQ(activeStack->getMovementRange(), 0);
		}
		battle()->activeStack = activeStack->unitId();
		callback = std::make_shared<PaviseCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<PaviseEnvironment>(gameState());
	}

	BattleAction choose(CStack * active)
	{
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		return evaluator.selectStackAction(active);
	}
};
}

TEST_F(NewHorizonsPaviseAITest, WithoutPaviseVisibleRangedThreatKeepsTheExistingWaitChoice)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NE(activeStack, nullptr);
	const auto healthBefore = activeStack->getAvailableHealth();
	const auto ordinaryAction = choose(activeStack);
	EXPECT_EQ(ordinaryAction.actionType, EActionType::WAIT);
	EXPECT_FALSE(activeStack->defended());
	EXPECT_EQ(activeStack->getAvailableHealth(), healthBefore);
}

TEST_F(NewHorizonsPaviseAITest, PaviseMakesDefendOutweighWaitUnderVisibleRangedThreatWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NE(activeStack, nullptr);
	const auto paviseHealthBefore = activeStack->getAvailableHealth();
	const auto paviseAction = choose(activeStack);
	EXPECT_EQ(paviseAction.actionType, EActionType::DEFEND);
	EXPECT_FALSE(activeStack->defended()) << "AI valuation must not apply Defend to live state";
	EXPECT_EQ(activeStack->getAvailableHealth(), paviseHealthBefore);
}

TEST_F(NewHorizonsPaviseAITest, PaviseDoesNotReplaceWaitWhenUsefulMovementIsAvailable)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false));
	ASSERT_NE(activeStack, nullptr);
	ASSERT_GT(activeStack->getMovementRange(), 0);
	const auto action = choose(activeStack);
	EXPECT_EQ(action.actionType, EActionType::WAIT)
		<< "A mobile melee stack should retain the ordinary wait-then-move plan";
	EXPECT_FALSE(activeStack->defended()) << "AI valuation must not apply Defend to live state";
	activeStack->waitedThisTurn = true;
	ASSERT_TRUE(activeStack->waitedThisTurn);
	const auto followup = choose(activeStack);
	EXPECT_EQ(followup.actionType, EActionType::MOVE)
		<< "After waiting, the stack should still advance rather than Defend";
}
