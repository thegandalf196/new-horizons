/*
 * CreaturePressureCachePreparationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/scripting/ScriptHandler.h"
#include <functional>

namespace
{
using Sample = damageCalculationTiming::Sample;
enum class Capability { CANONICAL, UNKNOWN, REVOKED_AFTER_CALCULATION, QUERY_FAILURE, CALCULATION_FAILURE };

class RecordingCalculator final : public IDamageCalculatorScript
{
	const IDamageCalculatorScript * delegate;
	Capability capability;
	mutable bool revoked = false;
public:
	mutable std::vector<std::pair<uint32_t, uint32_t>> calls;
	mutable std::string events;
	explicit RecordingCalculator(const IDamageCalculatorScript * delegate, Capability capability)
		: delegate(delegate), capability(capability) {}
	void reset() { calls.clear(); events.clear(); revoked = false; }
	DamageEstimation calculate(const CBattleInfoCallback & battle, DamageAttackInfo & info) const override
	{
		calls.emplace_back(info.attacker->unitId(), info.defender->unitId());
		events += 'C';
		if(capability == Capability::CALCULATION_FAILURE)
			throw std::runtime_error("fixture calculation failure");
		const auto answer = delegate->calculate(battle, info);
		revoked = capability == Capability::REVOKED_AFTER_CALCULATION;
		return answer;
	}
	DamageForecastImplementation forecastImplementation(const CBattleInfoCallback & battle) const override
	{
		events += 'Q';
		if(capability == Capability::QUERY_FAILURE)
			throw std::runtime_error("fixture capability-query failure");
		return capability == Capability::UNKNOWN || revoked ? DamageForecastImplementation::UNKNOWN
			: delegate->forecastImplementation(battle);
	}
};

class CalculatorFactory final : public IScriptFactory
{
	std::shared_ptr<IDamageCalculatorScript> calculator;
public:
	explicit CalculatorFactory(std::shared_ptr<IDamageCalculatorScript> calculator)
		: calculator(std::move(calculator)) {}
	void initialize(const ScriptTypeDescription &) override {}
	std::shared_ptr<IDamageCalculatorScript> createDamageCalculatorScript(const std::string &) const override { return calculator; }
	std::shared_ptr<ICombatEventScript> createCombatEventScript(const std::string &) const override { return nullptr; }
	std::shared_ptr<spells::effects::Effect> createSpellEffect(const std::string &) const override { return nullptr; }
};

// Serial fixture only. Queries use an exact identifier clone. Mock registration
// uses fresh LOADING storage, then restores the clone before any damage query.
// Exact original owners survive until restored, including on assertion/throw.
class ScopedCalculatorRegistry
{
	GameLibrary * library = LIBRARY;
	std::unique_ptr<CIdentifierStorage> identifiers;
	std::unique_ptr<ScriptHandler> scripts;
public:
	ScopedCalculatorRegistry()
	{
		auto isolatedIdentifiers = std::make_unique<CIdentifierStorage>(*library->identifiersHandler);
		auto isolatedScripts = std::make_unique<ScriptHandler>();
		identifiers = std::move(library->identifiersHandler);
		scripts = std::move(library->scriptTypeHandler);
		library->identifiersHandler = std::move(isolatedIdentifiers);
		library->scriptTypeHandler = std::move(isolatedScripts);
	}
	~ScopedCalculatorRegistry()
	{
		library->scriptTypeHandler = std::move(scripts);
		library->identifiersHandler = std::move(identifiers);
	}
	ScopedCalculatorRegistry(const ScopedCalculatorRegistry &) = delete;
	ScopedCalculatorRegistry & operator=(const ScopedCalculatorRegistry &) = delete;
	const IDamageCalculatorScript * ordinary() const { return scripts->getDamageCalculator(); }
	void install(const std::shared_ptr<RecordingCalculator> & calculator)
	{
		library->scriptTypeHandler->registerFactory(std::make_shared<CalculatorFactory>(calculator));
		auto registrationIdentifiers = std::make_unique<CIdentifierStorage>();
		auto queryIdentifiers = std::move(library->identifiersHandler);
		auto restore = vstd::makeScopeGuard([&]() { library->identifiersHandler = std::move(queryIdentifiers); });
		library->identifiersHandler = std::move(registrationIdentifiers);
		JsonNode descriptor;
		descriptor["script"].String() = "fixtureTerminalPressureCalculator";
		descriptor["implements"].String() = "damageCalculator";
		descriptor["priority"].Integer() = 0;
		library->scriptTypeHandler->loadObject("core", "fixtureTerminalPressureCalculator", descriptor);
	}
};

Sample measure(const std::function<void()> & prepare)
{
	Sample sample;
	{
		damageCalculationTiming::Binding binding(&sample);
		prepare();
	}
	return sample;
}

void expectCalculations(const Sample & sample, uint64_t expected)
{
	EXPECT_EQ(sample.initialCalls, expected);
	EXPECT_EQ(sample.nativePayload.calls, expected);
	EXPECT_EQ(sample.bonusPreparation.calls, expected);
	EXPECT_EQ(sample.luaCall.calls, expected);
	EXPECT_EQ(sample.luckySecondCalls, 0u);
	EXPECT_EQ(sample.unluckySecondCalls, 0u);
	EXPECT_EQ(sample.suppressedStages, 0u);
}
}

class CreaturePressureCachePreparationTest : public HeroCommandFixture
{
protected:
	bool legacyCommandRules = false;
	std::unique_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<HypotheticBattle> model;
	std::vector<uint32_t> ids;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(!legacyCommandRules)
			return;
		// Historical commands without Focus Fire; retain current hero/perk capture.
		const JsonNode config(JsonPath::builtin("config/newHorizonsCombatV2"));
		auto rules = config["combat"]["heroCommands"];
		rules["schemaVersion"].Integer() = 1;
		rules["rulesetVersion"].Integer() = 1;
		rules["commands"].Struct().erase("focusFire");
		heroCommands::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void prepare()
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			for(int row = 0; row < 2; ++row)
				ids.push_back(addStack(side, creatureByName("core:pikeman"),
					BattleHex(side == BattleSide::ATTACKER ? 3 : 12, 3 + row * 2), 10)->unitId());
		beginCombat();
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		auto source = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		model = std::make_shared<HypotheticBattle>(environment.get(), source);
		for(const auto & stack : battle()->stacks)
			(void)stack->counterAttacks.available();
		for(const auto id : ids)
		{
			(void)model->getForUpdate(id)->counterAttacks.available();
			for(const auto other : ids)
				ASSERT_EQ(model->battleGetAttackLuck(model->battleGetUnitByID(id), model->battleGetUnitByID(other), false), 0);
		}
	}
	std::vector<JsonNode> projectedStates() const
	{
		std::vector<JsonNode> result;
		for(const auto id : ids)
			result.push_back(model->getForUpdate(id)->save());
		return result;
	}
	void compareValues(DamageCache & full, DamageCache & terminal, const std::shared_ptr<HypotheticBattle> & state)
	{
		for(const auto first : ids)
			for(const auto second : ids)
			{
				const auto * actor = state->battleGetUnitByID(first);
				const auto * target = state->battleGetUnitByID(second);
				if(actor->unitSide() == target->unitSide())
					continue;
				EXPECT_EQ(full.getDamage(actor, target, state), terminal.getDamage(actor, target, state));
				EXPECT_EQ(full.getOriginalDamage(actor, target, state), terminal.getOriginalDamage(actor, target, state));
			}
	}
	void check(Capability capability, bool fast, bool parented = false, bool populated = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		EXPECT_EQ(heroCommands::supportedByRules(model->getHeroCommandRules(), HeroCommand::FOCUS_FIRE), !legacyCommandRules);
		const auto liveBefore = gameState()->saveToMemory();
		const auto battleBefore = projectedHitTest::serialized(*battle());
		const auto projectedBefore = projectedStates();
		const auto serverBefore = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameBefore = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto aiBefore = projectedHitTest::serialized(CRandomGenerator::getDefault());
		const auto * originalScripts = LIBRARY->scriptTypeHandler.get();
		const auto * originalIdentifiers = LIBRARY->identifiersHandler.get();
		{
			ScopedCalculatorRegistry registry;
			ASSERT_NE(registry.ordinary(), nullptr);
			ASSERT_EQ(registry.ordinary()->forecastImplementation(*model), DamageForecastImplementation::CANONICAL);
			auto calculator = std::make_shared<RecordingCalculator>(registry.ordinary(), capability);
			registry.install(calculator);
			DamageCache parent;
			if(parented)
				parent.buildDamageCache(model, BattleSide::ATTACKER);
			DamageCache full(parented ? &parent : nullptr), terminal(parented ? &parent : nullptr);
			if(populated)
			{
				full.cacheDamage(model->battleGetUnitByID(ids[0]), model->battleGetUnitByID(ids[2]), model);
				terminal.cacheDamage(model->battleGetUnitByID(ids[0]), model->battleGetUnitByID(ids[2]), model);
			}
			calculator->reset();
			const auto ordinary = measure([&]() { full.buildDamageCache(model, BattleSide::ATTACKER); });
			const auto calls = calculator->calls;
			EXPECT_EQ(calculator->events, std::string(8, 'C'));
			calculator->reset();
			const auto optimized = measure([&]() { terminal.buildCreaturePressureCache(model, BattleSide::ATTACKER); });
			expectCalculations(ordinary, 8);
			expectCalculations(optimized, fast ? 2 : 8);
			if(fast)
				EXPECT_EQ(calculator->calls, (std::vector<std::pair<uint32_t, uint32_t>>(calls.begin(), calls.begin() + 2)));
			else
				EXPECT_EQ(calculator->calls, calls);
			const bool queriesCapability = !legacyCommandRules && !parented && !populated;
			EXPECT_EQ(calculator->events, queriesCapability ? "CCQ" + std::string(fast ? 0 : 6, 'C') : std::string(8, 'C'));
			compareValues(full, terminal, model);
			auto changed = std::make_shared<HypotheticBattle>(environment.get(), model);
			auto victim = changed->getForUpdate(ids.front());
			int64_t damage = victim->getMaxHealth();
			victim->damage(damage);
			compareValues(full, terminal, changed);
		}
		EXPECT_EQ(LIBRARY->scriptTypeHandler.get(), originalScripts);
		EXPECT_EQ(LIBRARY->identifiersHandler.get(), originalIdentifiers);
		EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
		EXPECT_EQ(projectedHitTest::serialized(*battle()), battleBefore);
		EXPECT_EQ(projectedStates(), projectedBefore);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverBefore);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameBefore);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), aiBefore);
	}
	void checkFailure(bool missing)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto before = gameState()->saveToMemory();
		const auto battleBefore = projectedHitTest::serialized(*battle());
		const auto projectedBefore = projectedStates();
		const auto serverBefore = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameBefore = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto aiBefore = projectedHitTest::serialized(CRandomGenerator::getDefault());
		const auto * originalScripts = LIBRARY->scriptTypeHandler.get();
		const auto * originalIdentifiers = LIBRARY->identifiersHandler.get();
		{
			ScopedCalculatorRegistry registry;
			ASSERT_NE(registry.ordinary(), nullptr);
			auto calculator = std::make_shared<RecordingCalculator>(registry.ordinary(), Capability::CALCULATION_FAILURE);
			if(!missing)
				registry.install(calculator);
			for(const bool terminal : {false, true})
			{
				DamageCache cache;
				Sample sample;
				calculator->reset();
				std::string failure;
				try
				{
					damageCalculationTiming::Binding binding(&sample);
					if(terminal)
						cache.buildCreaturePressureCache(model, BattleSide::ATTACKER);
					else
						cache.buildDamageCache(model, BattleSide::ATTACKER);
				}
				catch(const std::runtime_error & error) { failure = error.what(); }
				EXPECT_EQ(failure, missing ? "No damage calculator script is loaded!" : "fixture calculation failure");
				EXPECT_EQ(sample.initialCalls, 1u);
				EXPECT_EQ(sample.nativePayload.calls, 1u);
				EXPECT_EQ(sample.luaCall.calls, 0u);
				EXPECT_EQ(calculator->events, missing ? "" : "C");
			}
		}
		EXPECT_EQ(LIBRARY->scriptTypeHandler.get(), originalScripts);
		EXPECT_EQ(LIBRARY->identifiersHandler.get(), originalIdentifiers);
		EXPECT_EQ(gameState()->saveToMemory(), before);
		EXPECT_EQ(projectedHitTest::serialized(*battle()), battleBefore);
		EXPECT_EQ(projectedStates(), projectedBefore);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverBefore);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameBefore);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), aiBefore);
	}
};

TEST_F(CreaturePressureCachePreparationTest, CanonicalFreshRootRetainsWarmupAndExactValues) { check(Capability::CANONICAL, true); }
TEST_F(CreaturePressureCachePreparationTest, UnsupportedCapturedRulesRetainFullPreparation)
{
	legacyCommandRules = true;
	check(Capability::CANONICAL, false);
}
TEST_F(CreaturePressureCachePreparationTest, ParentCacheRetainsFullPreparationAndOriginalValues) { check(Capability::CANONICAL, false, true); }
TEST_F(CreaturePressureCachePreparationTest, PopulatedRootRetainsFullPreparation) { check(Capability::CANONICAL, false, false, true); }
TEST_F(CreaturePressureCachePreparationTest, UnknownCalculatorRetainsFullCallOrder) { check(Capability::UNKNOWN, false); }
TEST_F(CreaturePressureCachePreparationTest, RevocationDuringWarmupRetainsFullCallOrder) { check(Capability::REVOKED_AFTER_CALCULATION, false); }
TEST_F(CreaturePressureCachePreparationTest, CapabilityQueryFailureRetainsFullCallOrder) { check(Capability::QUERY_FAILURE, false); }
TEST_F(CreaturePressureCachePreparationTest, MissingCalculatorFailsAtFirstActualCalculation) { checkFailure(true); }
TEST_F(CreaturePressureCachePreparationTest, CalculationFailureIsNotSwallowedByCapabilityFallback) { checkFailure(false); }
TEST_F(CreaturePressureCachePreparationTest, RegistryOwnersRestoreWhenScriptLoadThrows)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * scripts = LIBRARY->scriptTypeHandler.get();
	const auto * identifiers = LIBRARY->identifiersHandler.get();
	EXPECT_THROW(([&]()
	{
		ScopedCalculatorRegistry registry;
		registry.install(nullptr);
	})(), std::runtime_error);
	EXPECT_EQ(LIBRARY->scriptTypeHandler.get(), scripts);
	EXPECT_EQ(LIBRARY->identifiersHandler.get(), identifiers);
}
