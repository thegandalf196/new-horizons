/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../server/battles/BattleRoundHealing.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#endif
namespace
{
class NewHorizonsRisingTideTransitionTest : public HeroCommandFixture
{
protected:
	CStack * elemental = nullptr;
	CStack * enemy = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
	}
	void prepare(const char * creature = "core:iceElemental")
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:kalt")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode(creature)), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 10}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		elemental = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), 10);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
		for(auto * unit : {elemental, enemy})
		{
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE,
				BonusSource::OTHER, 1, BonusSourceID()));
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE,
					BonusSource::OTHER, unit == elemental ? 30 : 20, BonusSourceID(), BonusSubtypeID(), bound));
		}
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), elemental);
	}
	void injure(int64_t damage)
	{
		auto state = elemental->acquireState();
		state->damage(damage);
		publish(*state, -damage);
	}
	void publish(battle::CUnitState & state, int64_t delta = 0)
	{
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(elemental->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state.save();
		change.changedStacks.back().healthDelta = delta;
		gameHandler->sendAndApply(change);
	}
	void roundPhase()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
		battleRoundHealing::afterRound(*gameHandler, *battle());
	}
};
#if ENABLE_BATTLE_AI
class RisingTideEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RisingTideEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}
TEST_F(NewHorizonsRisingTideTransitionTest, ActualRoundTransitionGrowsBeforeSelectionWithoutActivationGrowth)
{
	prepare();
	ASSERT_GT(elemental->getMaxHealth(), 15u);
	injure(15);
	const auto wounded = elemental->getAvailableHealth();
	const auto count = elemental->getCount();
	const auto round = battle()->battleGetRound();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(elemental)));
	EXPECT_EQ(elemental->getAvailableHealth(), wounded);
	ASSERT_EQ(battle()->battleActiveUnit(), enemy);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeDefend(enemy)));
	EXPECT_EQ(battle()->battleGetRound(), round + 1);
	EXPECT_EQ(battle()->battleActiveUnit(), elemental);
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + elemental->getMaxHealth());
	EXPECT_EQ(elemental->getCount(), count + 1);
	battleRoundHealing::beforeTurn(*gameHandler, *battle(), elemental);
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + elemental->getMaxHealth());
}
TEST_F(NewHorizonsRisingTideTransitionTest, GrowthPreservesWoundsAndPermanentCasualtyDebt)
{
	prepare("core:waterElemental");
	ASSERT_GT(elemental->getMaxHealth(), 15u);
	const auto initialCount = elemental->getCount();
	const auto temporary = elemental->health.getResurrected();
	// prepare() has already begun Round1 and may have grown a temporary body.
	// Spend that temporary-first cohort AND one permanent creature, leaving a
	// wound. Later growth must not erase the permanent casualty or heal it.
	injure((static_cast<int64_t>(temporary) + 1) * elemental->getMaxHealth() + 15);
	const auto count = elemental->getCount();
	ASSERT_EQ(count, initialCount - temporary - 1);
	ASSERT_EQ(elemental->health.getResurrected(), 0);
	const auto wounded = elemental->getAvailableHealth();
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + elemental->getMaxHealth());
	EXPECT_EQ(elemental->getCount(), count + 1);
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + 2 * elemental->getMaxHealth());
	EXPECT_EQ(elemental->getCount(), count + 2);
	EXPECT_EQ(elemental->getSurvivingMissingHealth(), 15);
	EXPECT_EQ(elemental->health.getResurrected(), 2);
}
TEST_F(NewHorizonsRisingTideTransitionTest, TimeStopAndDestroyedStacksDoNotGrow)
{
	prepare();
	injure(15);
	elemental->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID()));
	const auto wounded = elemental->getAvailableHealth();
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded);
	elemental->removeBonuses(Selector::type()(BonusType::TIME_STOP));
	injure(elemental->getAvailableHealth());
	ASSERT_FALSE(elemental->alive());
	battleRoundHealing::afterRound(*gameHandler, *battle());
	EXPECT_FALSE(elemental->alive());
}
TEST_F(NewHorizonsRisingTideTransitionTest, RisingTideKeepsNativeArmyUnderHypnotize)
{
	prepare();
	injure(15);
	elemental->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(elemental), PlayerColor(1));
	const auto wounded = elemental->getAvailableHealth();
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + elemental->getMaxHealth());
}
TEST_F(NewHorizonsRisingTideTransitionTest, NativePreTurnRegenerationKeepsItsSeparateTimingAndWaitingGuard)
{
	prepare();
	injure(20);
	elemental->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HP_REGENERATION,
		BonusSource::OTHER, 4, BonusSourceID()));
	const auto wounded = elemental->getAvailableHealth();
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + elemental->getMaxHealth());
	battleRoundHealing::beforeTurn(*gameHandler, *battle(), elemental);
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + elemental->getMaxHealth() + 4);
	elemental->waiting = true;
	battleRoundHealing::beforeTurn(*gameHandler, *battle(), elemental);
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + elemental->getMaxHealth() + 4);
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsRisingTideTransitionTest, ProjectedRoundMatchesAuthoritativeGrowthAndActivationDoesNotRepeat)
{
	prepare();
	injure(15);
	RisingTideEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto wounded = elemental->getAvailableHealth();
	branch->nextRound();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded);
	roundPhase();
	const auto projected = branch->getForUpdate(elemental->unitId());
	EXPECT_EQ(projected->getAvailableHealth(), elemental->getAvailableHealth());
	EXPECT_EQ(projected->getCount(), elemental->getCount());
	branch->nextTurn(projected->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(projected->getAvailableHealth(), wounded + elemental->getMaxHealth());
}
TEST_F(NewHorizonsRisingTideTransitionTest, ExpiringSummonIsRemovedBeforeEitherRisingTideConsumer)
{
	prepare();
	injure(15);
	auto state = elemental->acquireState();
	state->summoned = true;
	state->summonRoundsRemaining = 1;
	publish(*state);
	RisingTideEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	branch->nextRound();
	roundPhase();
	EXPECT_FALSE(elemental->alive());
	const auto * projected = branch->battleGetUnitByID(elemental->unitId());
	EXPECT_TRUE(!projected || !projected->alive());
}
#endif
