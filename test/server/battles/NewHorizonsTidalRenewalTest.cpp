/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../server/battles/BattleRoundHealing.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#endif
namespace
{
class NewHorizonsTidalRenewalTest : public HeroCommandFixture
{
protected:
	bool historical = false;
	bool zero = false;
	CStack * elemental = nullptr;
	CStack * enemy = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
			rules["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().erase("tidalRenewalHealingHP");
		else if(zero)
			rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]["tidalRenewalHealingHP"].Integer() = 0;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
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
class RenewalEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RenewalEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}
TEST_F(NewHorizonsTidalRenewalTest, ActualRoundTransitionHealsBeforeSelectionWithoutActivationHealing)
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
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + 10);
	EXPECT_EQ(elemental->getCount(), count);
	battleRoundHealing::beforeTurn(*gameHandler, *battle(), elemental);
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + 10);
}
TEST_F(NewHorizonsTidalRenewalTest, OnlySurvivingTopWoundsRecoverAndCasualtiesStayLost)
{
	prepare("core:waterElemental");
	ASSERT_GT(elemental->getMaxHealth(), 15u);
	injure(elemental->getMaxHealth() + 15);
	const auto count = elemental->getCount();
	ASSERT_EQ(count, 9);
	const auto wounded = elemental->getAvailableHealth();
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + 10);
	EXPECT_EQ(elemental->getCount(), count);
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + 15);
	EXPECT_EQ(elemental->getCount(), count);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRoundHealing(*battle(), *elemental), 0);
}
TEST_F(NewHorizonsTidalRenewalTest, TimeStopAndDestroyedStacksDoNotHeal)
{
	prepare();
	injure(15);
	elemental->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID()));
	const auto wounded = elemental->getAvailableHealth();
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRoundHealing(*battle(), *elemental), 0);
	elemental->removeBonuses(Selector::type()(BonusType::TIME_STOP));
	injure(elemental->getAvailableHealth());
	ASSERT_FALSE(elemental->alive());
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRoundHealing(*battle(), *elemental), 0);
	battleRoundHealing::afterRound(*gameHandler, *battle());
	EXPECT_FALSE(elemental->alive());
}
TEST_F(NewHorizonsTidalRenewalTest, PassiveRenewalKeepsNativeArmyUnderHypnotize)
{
	prepare();
	injure(15);
	elemental->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(elemental), PlayerColor(1));
	const auto wounded = elemental->getAvailableHealth();
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + 10);
}
TEST_F(NewHorizonsTidalRenewalTest, HistoricalMissingKeyRetainsOldIdentityAndDoesNotHeal)
{
	historical = true;
	prepare();
	injure(15);
	const auto wounded = elemental->getAvailableHealth();
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded);
}
TEST_F(NewHorizonsTidalRenewalTest, ExplicitZeroSuppressesGenericWithoutHealing)
{
	zero = true;
	prepare();
	injure(15);
	const auto wounded = elemental->getAvailableHealth();
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded);
}
TEST_F(NewHorizonsTidalRenewalTest, NativePreTurnRegenerationKeepsItsSeparateTimingAndWaitingGuard)
{
	prepare();
	injure(20);
	elemental->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HP_REGENERATION,
		BonusSource::OTHER, 4, BonusSourceID()));
	const auto wounded = elemental->getAvailableHealth();
	roundPhase();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + 10);
	battleRoundHealing::beforeTurn(*gameHandler, *battle(), elemental);
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + 14);
	elemental->waiting = true;
	battleRoundHealing::beforeTurn(*gameHandler, *battle(), elemental);
	EXPECT_EQ(elemental->getAvailableHealth(), wounded + 14);
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsTidalRenewalTest, ProjectedRoundMatchesAuthoritativeHealingAndActivationDoesNotRepeat)
{
	prepare();
	injure(15);
	RenewalEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto wounded = elemental->getAvailableHealth();
	branch->nextRound();
	EXPECT_EQ(elemental->getAvailableHealth(), wounded);
	roundPhase();
	const auto projected = branch->getForUpdate(elemental->unitId());
	EXPECT_EQ(projected->getAvailableHealth(), elemental->getAvailableHealth());
	EXPECT_EQ(projected->getCount(), elemental->getCount());
	branch->nextTurn(projected->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(projected->getAvailableHealth(), wounded + 10);
}
TEST_F(NewHorizonsTidalRenewalTest, ExpiringSummonIsRemovedBeforeEitherRoundHealingConsumer)
{
	prepare();
	injure(15);
	auto state = elemental->acquireState();
	state->summoned = true;
	state->summonRoundsRemaining = 1;
	publish(*state);
	RenewalEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	branch->nextRound();
	roundPhase();
	EXPECT_FALSE(elemental->alive());
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRoundHealing(*battle(), *elemental), 0);
	const auto * projected = branch->battleGetUnitByID(elemental->unitId());
	EXPECT_TRUE(!projected || !projected->alive());
}
#endif
TEST_F(NewHorizonsTidalRenewalTest, ZeroRenewalKeyStillRejectsUnsupported1144Policy)
{
	const auto context = [](ESerializationVersion version)
	{
		CMemorySerializer wire;
		wire.oser.version = version;
		return newHorizonsHeroes::SpecialtySerializationContext::fromHandler(wire.oser);
	};
	JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	auto & policy = development["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	std::erase_if(policy.Struct(), [](const auto & row)
	{
		return row.first != "version" && row.first != "closeQuartersDrill" && row.first != "marshSharpshooter"
			&& row.first != "retaliationDamagePercent" && row.first != "defendDefense"
			&& row.first != "maximumHealthPercent" && row.first != "resistanceAuraPoints";
	});
	EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(development, context(ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES)));
	policy["tidalRenewalHealingHP"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(development, context(ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES)), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(development, context(ESerializationVersion::NEW_HORIZONS_ADJACENCY_FILTERS)), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(development, context(ESerializationVersion::CURRENT)));
}
