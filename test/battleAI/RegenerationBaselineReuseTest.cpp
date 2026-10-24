/* RegenerationBaselineReuseTest.cpp, VCMI / New Horizons, GPL v2.0 or later. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "HeroQueueBonusSnapshot.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../AI/BattleAI/RegenerationForecast.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/CStack.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/scripting/ScriptHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include <bit>
namespace
{
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
	void use(Capability next) { capability = next; reset(); }
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

}

class RegenerationBaselineReuseTest : public HeroCommandFixture
{
protected:
	std::unique_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<CBattleInfoCallback> before;
	std::shared_ptr<HypotheticBattle> base;
	std::vector<battle::Units> queue;
	DamageCache damage;
	uint32_t first = 0, second = 0;
	SpellID regeneration;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame(); startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		first = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(5, 5), 1)->unitId();
		second = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(6, 6), 1)->unitId();
		addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 30);
		beginCombat();
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		before = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		base = std::make_shared<HypotheticBattle>(environment.get(), before);
		for(const auto & unit : battle()->stacks)
			(void)unit->counterAttacks.available();
		base->battleGetTurnOrder(queue, 0, 4); // Exact existing Regeneration auxiliary horizon, not Hero exchange depth.
		damage.buildDamageCache(base, BattleSide::ATTACKER);
		regeneration = SpellID(SpellID::decode(std::string(newHorizonsMagic::NATURE_REGENERATION_SPELL)));
	}
	std::shared_ptr<HypotheticBattle> marked(uint32_t id)
	{
		// Deliberately authored post-cast state isolates the forecast/reuse boundary;
		// actual spell/cost/selection coverage remains in the existing regeneration AI suite.
		auto result = std::make_shared<HypotheticBattle>(environment.get(), before);
		auto unit = result->getForUpdate(id);
		int64_t wound = 80;
		unit->damage(wound);
		unit->regenerationRateMillionths = 500'000;
		unit->regenerationPendingMicroHealth = 40'000'000;
		Bonus effect(BonusDuration::N_TURNS, BonusType::HP_REGENERATION,
			BonusSource::SPELL_EFFECT, 0, BonusSourceID(regeneration));
		effect.turnsRemain = 3;
		result->addUnitBonus(id, {effect});
		return result;
	}
	auto states(HypotheticBattle & model)
	{
		std::map<uint32_t, std::pair<JsonNode, std::vector<std::string>>> result;
		for(const auto * unit : model.battleGetAllUnits(false))
		{
			auto current = model.getForUpdate(unit->unitId());
			result.emplace(unit->unitId(), std::make_pair(current->save(),
				heroQueueTest::bonusSnapshots(*current->getAllBonuses(Selector::all))));
		}
		return result;
	}
	float value(const std::shared_ptr<HypotheticBattle> & after, RegenerationBaselineReuse & reuse)
	{
		return projectedRegenerationValue(environment.get(), before, after, queue, regeneration,
			damage, BattleSide::ATTACKER, PlayerColor(0), 1.0f, &reuse);
	}
};

TEST_F(RegenerationBaselineReuseTest, DifferentTargetsKeepExactScoresAndStateWhileBaselineRunsOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	RegenerationBaselineReuse::TestControl off{false}, on;
	RegenerationBaselineReuse ordinary(&off), reused(&on);
	const auto world = gameState()->saveToMemory();
	const auto nativeRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto aiRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	for(const auto id : {first, second, first})
	{
		auto expected = marked(id), actual = marked(id);
		const auto beforeActual = states(*actual);
		const float reference = value(expected, ordinary);
		EXPECT_EQ(std::bit_cast<uint32_t>(reference), std::bit_cast<uint32_t>(value(actual, reused)));
		EXPECT_EQ(states(*actual), beforeActual);
	}
	EXPECT_EQ(on.completed, 1u); EXPECT_EQ(on.hits, 2u);
	EXPECT_EQ(off.completed, 0u); EXPECT_EQ(off.hits, 0u);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), nativeRng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), aiRng);
}

TEST_F(RegenerationBaselineReuseTest, ParentCacheSnapshotOwnsLayersAndRejectsNewPairEvenWithSameRoot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	DamageCache::Snapshot saved;
	{
		DamageCache localParent = damage;
		DamageCache child(&localParent);
		saved = child.captureSnapshot();
	}
	DamageCache parent = damage;
	DamageCache child(&parent);
	EXPECT_TRUE(child.matchesSnapshot(saved));
	parent.cacheDamage(base->battleGetUnitByID(first), base->battleGetUnitByID(second), base);
	EXPECT_FALSE(child.matchesSnapshot(saved));
}

TEST_F(RegenerationBaselineReuseTest, ExactQueueAndParentCacheChangesMissWithoutDiscardingOldLegalForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	RegenerationBaselineReuse::TestControl control;
	RegenerationBaselineReuse reuse(&control);
	value(marked(first), reuse);
	ASSERT_EQ(control.completed, 1u);
	std::reverse(queue.front().begin(), queue.front().end());
	RegenerationBaselineReuse::TestControl disabled{false};
	RegenerationBaselineReuse reference(&disabled);
	EXPECT_EQ(std::bit_cast<uint32_t>(value(marked(second), reuse)),
		std::bit_cast<uint32_t>(value(marked(second), reference)));
	EXPECT_EQ(control.completed, 2u); EXPECT_EQ(control.hits, 0u);
	damage.cacheDamage(base->battleGetUnitByID(first), base->battleGetUnitByID(second), base);
	EXPECT_EQ(std::bit_cast<uint32_t>(value(marked(first), reuse)),
		std::bit_cast<uint32_t>(value(marked(first), reference)));
	EXPECT_EQ(control.completed, 3u); EXPECT_EQ(control.hits, 0u);
}

TEST_F(RegenerationBaselineReuseTest, IncompleteBaselineCannotPublishReusableObservations)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	RegenerationBaselineReuse::TestControl control;
	RegenerationBaselineReuse reuse(&control);
	auto session = reuse.prepare(environment.get(), *before, queue, damage, BattleSide::ATTACKER, PlayerColor(0));
	ASSERT_TRUE(session.recording);
	EXPECT_THROW(reuse.complete(std::move(session), {}), std::logic_error);
	EXPECT_EQ(control.completed, 0u); EXPECT_EQ(control.hits, 0u);
	auto retry = reuse.prepare(environment.get(), *before, queue, damage, BattleSide::ATTACKER, PlayerColor(0));
	EXPECT_TRUE(retry.recording); EXPECT_EQ(retry.observations, nullptr);
}

TEST_F(RegenerationBaselineReuseTest, UnknownOrThrowingCalculatorCapabilityRetainsOrdinaryPath)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * calculator = LIBRARY->scriptTypes()->getDamageCalculator();
	for(const auto capability : {Capability::UNKNOWN, Capability::QUERY_FAILURE})
	{
		auto wrapped = std::make_shared<RecordingCalculator>(calculator, capability);
		ScopedCalculatorRegistry registry;
		registry.install(wrapped);
		RegenerationBaselineReuse::TestControl off{false}, on;
		RegenerationBaselineReuse ordinary(&off), reused(&on);
		EXPECT_EQ(std::bit_cast<uint32_t>(value(marked(first), ordinary)),
			std::bit_cast<uint32_t>(value(marked(first), reused)));
		EXPECT_EQ(on.completed, 0u); EXPECT_EQ(on.hits, 0u);
	}
}

TEST_F(RegenerationBaselineReuseTest, ExistingBaselineMarksAndDeniedActivationKeepExactSubtraction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto original = marked(first);
	before = original;
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 0, BonusSourceID(regeneration));
	original->addUnitBonus(first, {stopped});
	base = std::make_shared<HypotheticBattle>(environment.get(), before);
	damage = DamageCache();
	damage.buildDamageCache(base, BattleSide::ATTACKER);
	RegenerationBaselineReuse::TestControl off{false}, on;
	RegenerationBaselineReuse ordinary(&off), reuse(&on);
	const auto initial = states(*original);
	for(const auto id : {second, first, second})
	{
		auto expected = marked(id), actual = marked(id);
		expected->getForUpdate(id)->regenerationRateMillionths = 1'000'000;
		actual->getForUpdate(id)->regenerationRateMillionths = 1'000'000;
		EXPECT_EQ(std::bit_cast<uint32_t>(value(actual, reuse)),
			std::bit_cast<uint32_t>(value(expected, ordinary)));
	}
	EXPECT_EQ(on.completed, 1u); EXPECT_EQ(on.hits, 2u);
	EXPECT_EQ(states(*original), initial);
}

TEST_F(RegenerationBaselineReuseTest, CalculationAbortCannotPublishAndSuccessfulRetryCanReuse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto world = gameState()->saveToMemory();
	const auto nativeRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto aiRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ScopedCalculatorRegistry registry;
	auto calculator = std::make_shared<RecordingCalculator>(registry.ordinary(), Capability::CALCULATION_FAILURE);
	registry.install(calculator);
	RegenerationBaselineReuse::TestControl on, off{false};
	RegenerationBaselineReuse reuse(&on), ordinary(&off);
	auto after = marked(first);
	const auto unchanged = states(*after);
	EXPECT_THROW(value(after, reuse), std::runtime_error);
	EXPECT_FALSE(calculator->calls.empty());
	EXPECT_EQ(on.completed, 0u); EXPECT_EQ(on.hits, 0u);
	EXPECT_EQ(states(*after), unchanged);
	calculator->use(Capability::CANONICAL);
	const auto expected = std::bit_cast<uint32_t>(value(marked(first), ordinary));
	EXPECT_EQ(std::bit_cast<uint32_t>(value(after, reuse)), expected);
	EXPECT_EQ(on.completed, 1u); EXPECT_EQ(on.hits, 0u);
	EXPECT_EQ(std::bit_cast<uint32_t>(value(after, reuse)), expected);
	EXPECT_EQ(on.completed, 1u); EXPECT_EQ(on.hits, 1u);
	EXPECT_EQ(states(*after), unchanged);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), nativeRng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), aiRng);
}

TEST_F(RegenerationBaselineReuseTest, WarmedHitThenCalculationRevocationOrQueryFailureRunsOrdinaryForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto world = gameState()->saveToMemory();
	const auto nativeRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto aiRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ScopedCalculatorRegistry registry;
	auto calculator = std::make_shared<RecordingCalculator>(registry.ordinary(), Capability::CANONICAL);
	registry.install(calculator);
	RegenerationBaselineReuse::TestControl on, off{false};
	RegenerationBaselineReuse reuse(&on), ordinary(&off);
	value(marked(first), reuse);
	value(marked(second), reuse);
	ASSERT_EQ(on.completed, 1u); ASSERT_EQ(on.hits, 1u);
	calculator->use(Capability::REVOKED_AFTER_CALCULATION);
	value(marked(first), reuse); // Canonical at lookup; this real forecast revokes capability.
	ASSERT_FALSE(calculator->calls.empty());
	ASSERT_EQ(on.hits, 2u);
	for(const auto capability : {Capability::UNKNOWN, Capability::QUERY_FAILURE})
	{
		if(capability == Capability::QUERY_FAILURE)
			calculator->use(capability);
		// First iteration retains the actual calculation-triggered revocation.
		auto after = marked(second);
		const auto unchanged = states(*after);
		const auto calls = calculator->calls.size();
		EXPECT_EQ(std::bit_cast<uint32_t>(value(after, reuse)),
			std::bit_cast<uint32_t>(value(marked(second), ordinary)));
		EXPECT_GT(calculator->calls.size(), calls);
		EXPECT_EQ(on.completed, 1u); EXPECT_EQ(on.hits, 2u);
		EXPECT_EQ(states(*after), unchanged);
	}
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), nativeRng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), aiRng);
}
