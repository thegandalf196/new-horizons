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
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"

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
		// Paired immobile controls need a nonlethal shot: 100 Titans kill all
		// Pikemen even with Pavise, leaving zero prevented HP after the cap.
		auto * shooter = addStack(BattleSide::DEFENDER, creatureByName("core:titan"),
			BattleHex(12, 5), immobilize ? 1 : 100);
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
			// Isolate a ranged threat: a mobile Titan can instead close to melee,
			// where Pavise does not protect and the mode comparison picks that hit.
			immobilized.val = -shooter->getMovementRange();
			shooter->addNewBonus(std::make_shared<Bonus>(immobilized));
			ASSERT_EQ(shooter->getMovementRange(), 0);
			ASSERT_TRUE(battle()->battleCanShoot(shooter, activeStack->getPosition()));
			ASSERT_TRUE(battle()->meleeAttackHexes(shooter, activeStack, shooter->getPosition()).empty());
			for(const auto & position : battle()->battleGetAvailableHexes(shooter, false))
			{
				ASSERT_TRUE(battle()->meleeAttackHexes(shooter, activeStack, position).empty());
			}
		}
		battle()->activeStack = activeStack->unitId();
		callback = std::make_shared<PaviseCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<PaviseEnvironment>(gameState());
		if(immobilize)
		{
			auto baseline = std::make_shared<HypotheticBattle>(environment.get(),
				std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
			auto defended = std::make_shared<HypotheticBattle>(environment.get(), baseline);
			auto projected = defended->getForUpdate(activeStack->unitId());
			projected->defending = true;
			projected->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::STACK_GETS_TURN,
				BonusType::UNIT_DEFENDING, BonusSource::OTHER, 0, BonusSourceID())});
			const auto before = baseline->battleEstimateDamage(BattleAttackInfo(shooter, activeStack, 0, true)).damage;
			const auto after = defended->battleEstimateDamage(BattleAttackInfo(
				defended->battleGetUnitByID(shooter->unitId()), projected.get(), 0, true)).damage;
			ASSERT_LT(before.max, activeStack->getAvailableHealth());
			ASSERT_LT(after.max, activeStack->getAvailableHealth());
			if(pavise)
			{
				ASSERT_GT((before.min + before.max) / 2 - (after.min + after.max) / 2,
					std::max<int64_t>(1, activeStack->getAvailableHealth() / 20));
			}
		}
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
	EXPECT_EQ(followup.actionType, EActionType::WALK)
		<< "After waiting, the stack should still advance rather than Defend";
}
