/*
 * DamageFinalizationParityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/scripting/ScriptHandler.h"
#include <any>
#include <bit>
#include <type_traits>
#include <array>
#include <tuple>
#include <stdexcept>

namespace
{
enum class Capability { CANONICAL, UNKNOWN, QUERY_FAILURE };
class RecordingCalculator final : public IDamageCalculatorScript
{
	const IDamageCalculatorScript * delegate;
	Capability capability;
public:
	mutable std::string events;
	mutable std::vector<DamageAttackInfo> payloads;
	explicit RecordingCalculator(const IDamageCalculatorScript * delegate,
		Capability capability = Capability::CANONICAL) : delegate(delegate), capability(capability) {}
	DamageEstimation calculate(const CBattleInfoCallback & battle, DamageAttackInfo & payload) const override
	{
		events += 'C';
		const auto answer = delegate->calculate(battle, payload);
		payloads.push_back(payload); // Includes the real calculator's declared bonus maps.
		return answer;
	}
	DamageForecastImplementation forecastImplementation(const CBattleInfoCallback & battle) const override
	{
		events += 'Q';
		if(capability == Capability::QUERY_FAILURE)
			throw std::runtime_error("fixture capability failure");
		return capability == Capability::UNKNOWN ? DamageForecastImplementation::UNKNOWN
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


// Uses the scripting field list so every current native payload field is checked,
// including identity, positions, all independent multipliers and explicit zeros.
struct PayloadFields
{
	std::map<std::string, std::any> values;
	template<class T> void operator()(const char * name, T & value, const char *)
	{
		values.emplace(name, value);
	}
};
struct CompareFields
{
	const PayloadFields & expected;
	std::size_t count = 0;
	template<class T> void operator()(const char * name, T & value, const char *)
	{
		SCOPED_TRACE(name);
		ASSERT_TRUE(expected.values.contains(name));
		if constexpr(std::is_floating_point_v<T>)
		{
			using Bits = std::array<std::byte, sizeof(T)>;
			EXPECT_EQ(std::bit_cast<Bits>(value), std::bit_cast<Bits>(std::any_cast<T>(expected.values.at(name))));
		}
		else
		{
			EXPECT_EQ(value, std::any_cast<T>(expected.values.at(name)));
		}
		++count;
	}
};
void expectPayload(DamageAttackInfo expected, DamageAttackInfo actual)
{
	PayloadFields fields;
	expected.serializeScript(fields);
	CompareFields compare{fields};
	actual.serializeScript(compare);
	EXPECT_EQ(compare.count, fields.values.size());
}
auto allEstimationFields(const DamageEstimation & value)
{
	return std::make_tuple(value.damage.min, value.damage.max, value.kills.min, value.kills.max,
		value.damageBeforeDefense.min, value.damageBeforeDefense.max,
		value.attackerOrderCause, value.defenderOrderCause,
		value.attackerOrderCauses, value.defenderOrderCauses,
		value.archeryDefenseIgnorePercent, value.archeryCrossfireDamagePercent,
		value.archeryDeadeye, value.archeryHighArc,
		value.guardianSpiritAbsorbedDamage, value.guardianSpiritOverflowDamage);
}
}

class DamageFinalizationParityTest : public ProjectedTargetHitFixture
{
protected:
	using Saved = std::array<std::vector<std::byte>, 5>;
	Saved nativeSnapshot()
	{
		return {gameState()->saveToMemory(), projectedHitTest::serialized(*battle()),
			projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}
	std::vector<JsonNode> projectedSnapshot()
	{
		std::vector<JsonNode> result;
		for(const auto * unit : child->battleGetAllUnits(false))
		{
			result.push_back(child->getForUpdate(unit->unitId())->save());
			const auto bonusOwner = unit->getUnstackedBonuses(Selector::all);
			for(const auto & bonus : *bonusOwner)
				result.push_back(bonus->toJsonNode());
		}
		return result;
	}
	void parity(const CBattleInfoCallback & callback, BattleAttackInfo attack,
		RecordingCalculator & recorder )
	{
		const auto native = nativeSnapshot();
		const auto projected = projectedSnapshot();
		const auto before = recorder.payloads.size();
		const auto ordinary = callback.calculateDmgRange(attack);
		const auto actual = callback.calculateDmgRange(attack);
		ASSERT_EQ(recorder.payloads.size(), before + 2);
		expectPayload(recorder.payloads.at(before), recorder.payloads.at(before + 1));
		EXPECT_EQ(allEstimationFields(actual), allEstimationFields(ordinary));
		EXPECT_GE(actual.damage.min, 0);
		EXPECT_GE(actual.damage.max, actual.damage.min);
		EXPECT_GE(actual.kills.min, 0);
		EXPECT_GE(actual.kills.max, actual.kills.min);
		EXPECT_EQ(nativeSnapshot(), native);
		EXPECT_EQ(projectedSnapshot(), projected);
	}
};

TEST_F(DamageFinalizationParityTest, OrdinaryFinalizationRetainsFullPayloadAndEstimation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ScopedCalculatorRegistry registry;
	ASSERT_NE(registry.ordinary(), nullptr);
	auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary());
	registry.install(recorder);
	BattleAttackInfo attack(child->getForUpdate(incoming->unitId()).get(),
		child->getForUpdate(braced->unitId()).get(), 0, false);
	ASSERT_NO_FATAL_FAILURE(parity(*child, attack, *recorder));
	attack.luckyStrike = true;
	ASSERT_NO_FATAL_FAILURE(parity(*child, attack, *recorder));
	attack.luckyStrike = false;
	attack.unluckyStrike = true;
	ASSERT_NO_FATAL_FAILURE(parity(*child, attack, *recorder));
	EXPECT_EQ(recorder->events, "CCCCCC");
}

namespace
{
// Deliberately override ONLY the full getter. The new default must dispatch here,
// not bypass this custom/proxy policy by forwarding to its underlying subject.
class FullLuckPolicyProxy final : public BattleProxy
{
public:
	LuckRollRules rules;
	mutable int fullQueries = 0;
	bool throwOnQuery = false;
	explicit FullLuckPolicyProxy(Subject subject) : BattleProxy(std::move(subject)) {}
	LuckRollRules getLuckRollRules() const override
	{
		++fullQueries;
		if(throwOnQuery)
			throw std::runtime_error("custom Luck rules query failed");
		return rules;
	}
};

LuckRollRules rawLuckPolicy(bool finalMultiplier, int shape)
{
	LuckRollRules rules;
	rules.finalDirectPhysicalMultiplier = finalMultiplier;
	if(shape != 0)
	{
		rules.goodChance = {5, 10, 20};
		rules.badChance = {3, 7, 15};
	}
	rules.diceSize = shape == 2 ? 100 : 0;
	rules.affectsAllTargets = true;
	return rules;
}
}

TEST_F(DamageFinalizationParityTest, RawLuckDefaultUsesCustomFullGetterForEveryCurveShape)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	FullLuckPolicyProxy proxy(live);
	for(bool flag : {false, true})
		for(int shape : {0, 1, 2})
		{
			proxy.rules = rawLuckPolicy(flag, shape);
			const auto before = proxy.rules;
			const int queries = proxy.fullQueries;
			EXPECT_EQ(proxy.getRawFinalDirectPhysicalLuckPolicy(), flag);
			EXPECT_EQ(proxy.fullQueries, queries + 1);
			EXPECT_EQ(proxy.rules, before);
		}
}

TEST_F(DamageFinalizationParityTest, RawLuckDefaultPreservesThrowingGetterAndNullActorOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	FullLuckPolicyProxy proxy(live);
	proxy.throwOnQuery = true;
	EXPECT_EQ(proxy.battleGetAttackLuck(nullptr, braced, false), 0);
	EXPECT_EQ(proxy.fullQueries, 0);
	EXPECT_THROW(proxy.getRawFinalDirectPhysicalLuckPolicy(), std::runtime_error);
	EXPECT_EQ(proxy.fullQueries, 1);
	EXPECT_THROW(proxy.battleGetAttackLuck(incoming, braced, false), std::runtime_error);
	EXPECT_EQ(proxy.fullQueries, 2);
	BattleAttackInfo attack(incoming, braced, 0, false);
	EXPECT_THROW(proxy.calculateDmgRange(attack), std::runtime_error);
}

TEST_F(DamageFinalizationParityTest, RawLuckHypotheticSnapshotStaysIsolatedWithoutParentScalarQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto proxy = std::make_shared<FullLuckPolicyProxy>(live);
	proxy->rules = rawLuckPolicy(true, 1); // Nonempty curves with zero-dice legacy fallback.
	const auto captured = proxy->rules;
	child = std::make_shared<HypotheticBattle>(environment.get(), proxy);
	proxy->rules = rawLuckPolicy(false, 2);
	const int queries = proxy->fullQueries;
	proxy->throwOnQuery = true;
	for(int query = 0; query < 10; ++query)
		EXPECT_TRUE(child->getRawFinalDirectPhysicalLuckPolicy());
	EXPECT_EQ(proxy->fullQueries, queries);
	EXPECT_EQ(child->getLuckRollRules(), captured);
	HypotheticBattle sibling(environment.get(), child);
	EXPECT_TRUE(sibling.getRawFinalDirectPhysicalLuckPolicy());
	EXPECT_EQ(sibling.getLuckRollRules(), captured);
	EXPECT_EQ(proxy->fullQueries, queries);
}

TEST_F(DamageFinalizationParityTest, RawLuckScalarAndFullGetterFallbackKeepDamagePayloadBitsAndState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ScopedCalculatorRegistry registry;
	ASSERT_NE(registry.ordinary(), nullptr);
	auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary());
	registry.install(recorder);
	for(bool flag : {false, true})
		for(int shape : {0, 1, 2})
		{
			auto parent = std::make_shared<FullLuckPolicyProxy>(live);
			parent->rules = rawLuckPolicy(flag, shape);
			child = std::make_shared<HypotheticBattle>(environment.get(), parent);
			FullLuckPolicyProxy reference(child);
			reference.rules = parent->rules;
			const auto native = nativeSnapshot();
			const auto projected = projectedSnapshot();
			for(bool physical : {false, true})
				for(int luck : {-1, 0, 1})
				{
					BattleAttackInfo attack(child->getForUpdate(incoming->unitId()).get(),
						child->getForUpdate(braced->unitId()).get(), 0, false);
					attack.physicalDamage = physical;
					attack.luckyStrike = luck > 0;
					attack.unluckyStrike = luck < 0;
					const auto offset = recorder->payloads.size();
					const auto expected = reference.calculateDmgRange(attack);
					const auto actual = child->calculateDmgRange(attack);
					ASSERT_EQ(recorder->payloads.size(), offset + 2);
					expectPayload(recorder->payloads[offset], recorder->payloads[offset + 1]);
					EXPECT_EQ(allEstimationFields(actual), allEstimationFields(expected));
					EXPECT_EQ(child->battleGetAttackLuck(attack.attacker, attack.defender, false),
						reference.battleGetAttackLuck(attack.attacker, attack.defender, false));
					EXPECT_EQ(recorder->payloads[offset + 1].newHorizonsFinalLuck, flag && physical);
				}
			EXPECT_EQ(nativeSnapshot(), native);
			EXPECT_EQ(projectedSnapshot(), projected);
		}
}
