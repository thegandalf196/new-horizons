/*
 * PrimaryFortuneProducerTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/scripting/ScriptHandler.h"
#include <any>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace
{
class RecordingCalculator final : public IDamageCalculatorScript
{
	const IDamageCalculatorScript * delegate;
	unsigned throwAt;
public:
	mutable unsigned queries = 0;
	mutable std::vector<DamageAttackInfo> calls;
	explicit RecordingCalculator(const IDamageCalculatorScript * value, unsigned throwAt = 0)
		: delegate(value), throwAt(throwAt) {}
	DamageForecastImplementation forecastImplementation(const CBattleInfoCallback &) const override
	{
		++queries;
		return DamageForecastImplementation::UNKNOWN;
	}
	DamageEstimation calculate(const CBattleInfoCallback & battle, DamageAttackInfo & payload) const override
	{
		calls.push_back(payload);
		if(calls.size() == throwAt)
			throw std::runtime_error("Fortune producer calculator failure");
		const auto answer = delegate->calculate(battle, payload);
		calls.back() = payload;
		return answer;
	}
	void clear() { calls.clear(); queries = 0; }
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
		EXPECT_EQ(value, std::any_cast<T>(expected.values.at(name)));
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

void expectCalls(const std::vector<DamageAttackInfo> & expected, const std::vector<DamageAttackInfo> & actual)
{
	ASSERT_EQ(actual.size(), expected.size());
	for(size_t index = 0; index < expected.size(); ++index)
	{
		SCOPED_TRACE(index);
		auto left = expected[index];
		auto right = actual[index];
		ASSERT_NE(left.attacker, nullptr);
		ASSERT_NE(right.attacker, nullptr);
		ASSERT_NE(left.defender, nullptr);
		ASSERT_NE(right.defender, nullptr);
		EXPECT_EQ(left.attacker->unitId(), right.attacker->unitId());
		EXPECT_EQ(left.defender->unitId(), right.defender->unitId());
		// Branch ownership necessarily differs; every other payload field stays exact.
		left.attacker = right.attacker = nullptr;
		left.defender = right.defender = nullptr;
		expectPayload(left, right);
	}
}
}

class PrimaryFortuneProducerTest : public ProjectedTargetHitFixture
{
protected:
	uint32_t actorId = 0, targetId = 0, otherId = 0, allyId = 0;
	void prepareProducer()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		actorId = incoming->unitId();
		targetId = braced->unitId();
		otherId = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 100)->unitId();
		resetChild();
		child->setPerfectFortuneState(BattleSide::DEFENDER, {true, false});
		warm(child);
	}
	void warm(const std::shared_ptr<HypotheticBattle> & branch)
	{
		for(const auto * unit : branch->battleGetAllUnits(false))
			(void)branch->getForUpdate(unit->unitId())->counterAttacks.available();
	}
	using Bytes = std::vector<std::byte>;
	std::vector<Bytes> nativeSnapshot()
	{
		return {gameState()->saveToMemory(), projectedHitTest::serialized(*battle()),
			projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}
	std::vector<Bytes> projectedSnapshot(const std::shared_ptr<HypotheticBattle> & branch)
	{
		std::vector<Bytes> result;
		for(const auto * unit : branch->battleGetAllUnits(false))
		{
			const auto projected = branch->getForUpdate(unit->unitId());
			result.push_back(projectedHitTest::serialized(projected->save()));
			const auto owner = projected->getUnstackedBonuses(Selector::all);
			std::vector<JsonNode> bonuses;
			for(const auto & bonus : *owner)
				bonuses.push_back(bonus->toJsonNode());
			result.push_back(projectedHitTest::serialized(bonuses));
		}
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			result.push_back(projectedHitTest::serialized(branch->getSylvanLuckState(side)));
			result.push_back(projectedHitTest::serialized(branch->getPerfectFortuneState(side)));
			result.push_back(projectedHitTest::serialized(branch->getLuckSerendipityState(side)));
			result.push_back(projectedHitTest::serialized(branch->getAdverseCombatRerollState(side)));
			result.push_back(projectedHitTest::serialized(branch->getRelentlessAssaultState(side)));
			result.push_back(projectedHitTest::serialized(branch->getHeroOrderStates(side)));
			result.push_back(projectedHitTest::serialized(branch->getHeroActionAllowances(side)));
			result.push_back(projectedHitTest::serialized(branch->getWarcastingState(side)));
		}
		result.push_back(projectedHitTest::serialized(branch->getActiveStackID()));
		result.push_back(projectedHitTest::serialized(branch->battleGetRound()));
		result.push_back(projectedHitTest::serialized(branch->getElementalRebirthSpawnUnitIds()));
		result.push_back(projectedHitTest::serialized(branch->getProjectedPrimalBurstHits().size()));
		return result;
	}
	float strike(const std::shared_ptr<HypotheticBattle> & branch, uint32_t target, bool probe)
	{
		DamageCache cache;
		BattleExchangeVariant exchange;
		return exchange.trackAttack(branch->getForUpdate(actorId), branch->getForUpdate(target),
			false, true, cache, branch, probe, false);
	}
};

TEST_F(PrimaryFortuneProducerTest, TwoTargetProbesMatchAppliedPrimaryScoresAndPreserveState)
{
	ASSERT_NO_FATAL_FAILURE(prepareProducer());
	const auto native = nativeSnapshot();
	const auto state = projectedSnapshot(child);
	for(const auto id : {targetId, otherId})
	{
		SCOPED_TRACE(id);
		auto applied = std::make_shared<HypotheticBattle>(environment.get(), child);
		warm(applied);
		const auto health = child->getForUpdate(id)->getAvailableHealth();
		const auto first = strike(child, id, true);
		const auto repeated = strike(child, id, true);
		const auto appliedScore = strike(applied, id, false);
		EXPECT_TRUE(std::isfinite(first));
		EXPECT_GT(first, 0);
		EXPECT_EQ(std::bit_cast<uint32_t>(first), std::bit_cast<uint32_t>(repeated));
		EXPECT_EQ(std::bit_cast<uint32_t>(first), std::bit_cast<uint32_t>(appliedScore));
		EXPECT_LT(applied->getForUpdate(id)->getAvailableHealth(), health);
		EXPECT_TRUE(applied->getPerfectFortuneState(BattleSide::DEFENDER).used);
		EXPECT_FALSE(child->getPerfectFortuneState(BattleSide::DEFENDER).used);
		EXPECT_EQ(projectedSnapshot(child), state);
		EXPECT_EQ(nativeSnapshot(), native);
	}
}

TEST_F(PrimaryFortuneProducerTest, AppliedLethalPrimaryConsumesPreHitFortuneAndNextStrikeIsOrdinary)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex - 1), 1);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	auto * next = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 1), 100);
	activate(source->unitId());
	environment = std::make_unique<projectedHitTest::Environment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	resetChild();
	actorId = source->unitId(); targetId = victim->unitId(); otherId = next->unitId(); allyId = ally->unitId();
	SylvanLuckState fortune;
	fortune.forestsFavor = fortune.luckyRecovery = fortune.sharedFortune = fortune.cascadingFortune = true;
	child->setSylvanLuckState(BattleSide::ATTACKER, fortune);
	child->setPerfectFortuneState(BattleSide::ATTACKER, {true, false});
	warm(child);
	auto actor = child->getForUpdate(actorId);
	auto target = child->getForUpdate(targetId);
	int64_t wound = 50;
	actor->damage(wound);
	const auto health = actor->getAvailableHealth();
	const auto victimHealth = target->getAvailableHealth();
	ASSERT_EQ(victimHealth, 200);
	BattleAttackInfo first(actor.get(), target.get(), 0, false);
	ASSERT_TRUE(child->battleCanTriggerPerfectFortune(first));
	ASSERT_GE(child->battleExpectedLuckDamage(first), victimHealth);
	const auto native = nativeSnapshot();
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), child);
	warm(sibling);
	const auto siblingState = projectedSnapshot(sibling);
	EXPECT_TRUE(std::isfinite(strike(child, targetId, false)));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(actor->getAvailableHealth(), health + 20); // Actual200HP /10; no overkill healing.
	EXPECT_TRUE(child->getPerfectFortuneState(BattleSide::ATTACKER).used);
	const auto committed = child->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_TRUE(committed.positiveLuckUnits.contains(actorId));
	EXPECT_TRUE(committed.speedUnits.contains(actorId));
	EXPECT_TRUE(committed.sharedUnits.contains(allyId));
	EXPECT_TRUE(committed.cascadingPending);
	EXPECT_EQ(projectedSnapshot(sibling), siblingState);
	EXPECT_EQ(nativeSnapshot(), native);
	BattleAttackInfo second(actor.get(), child->getForUpdate(otherId).get(), 0, false);
	ASSERT_FALSE(child->battleCanTriggerPerfectFortune(second));
	ASSERT_EQ(child->captureFortuneStrikeOutcome(second), ProjectedLuckOutcome::NEUTRAL);
	const auto nextHealth = child->getForUpdate(otherId)->getAvailableHealth();
	const auto healingBefore = actor->getAvailableHealth();
	const auto expectedDamage = child->battleExpectedLuckDamage(second);
	EXPECT_TRUE(std::isfinite(strike(child, otherId, false)));
	EXPECT_EQ(child->getForUpdate(otherId)->getAvailableHealth(), nextHealth - expectedDamage);
	EXPECT_EQ(actor->getAvailableHealth(), healingBefore);
	EXPECT_EQ(child->getSylvanLuckState(BattleSide::ATTACKER), committed);
	EXPECT_TRUE(child->getPerfectFortuneState(BattleSide::ATTACKER).used);
	EXPECT_EQ(projectedSnapshot(sibling), siblingState);
	EXPECT_EQ(nativeSnapshot(), native);
}

TEST_F(PrimaryFortuneProducerTest, UnknownCalculatorRetainsPayloadOrderAndFailuresThroughActualProducer)
{
	ASSERT_NO_FATAL_FAILURE(prepareProducer());
	const auto * scriptsBefore = LIBRARY->scriptTypeHandler.get();
	const auto * identifiersBefore = LIBRARY->identifiersHandler.get();
	for(const unsigned throwAt : {0u, 1u, 2u})
	{
		SCOPED_TRACE(throwAt);
		ScopedCalculatorRegistry registry;
		ASSERT_NE(registry.ordinary(), nullptr);
		auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary(), throwAt);
		registry.install(recorder);
		auto probe = std::make_shared<HypotheticBattle>(environment.get(), child);
		auto applied = std::make_shared<HypotheticBattle>(environment.get(), child);
		warm(probe); warm(applied);
		const auto native = nativeSnapshot();
		const auto probeState = projectedSnapshot(probe);
		const auto appliedState = projectedSnapshot(applied);
		float values[2] = {};
		std::string errors[2];
		std::vector<DamageAttackInfo> calls[2];
		for(unsigned mode = 0; mode < 2; ++mode)
		{
			recorder->clear();
			try { values[mode] = strike(mode == 0 ? probe : applied, targetId, mode == 0); }
			catch(const std::runtime_error & error) { errors[mode] = error.what(); }
			calls[mode] = recorder->calls;
			EXPECT_EQ(recorder->queries, 0u); // Unknown calculators need no new capability gate.
		}
		ASSERT_GE(calls[0].size(), throwAt ? throwAt : 2u);
		expectCalls(calls[0], calls[1]);
		EXPECT_FALSE(calls[0][0].luckyStrike);
		if(calls[0].size() >= 2)
		{
			EXPECT_TRUE(calls[0][1].luckyStrike);
		}
		if(throwAt)
		{
			EXPECT_EQ(calls[0].size(), throwAt);
			EXPECT_EQ(errors[0], "Fortune producer calculator failure");
			EXPECT_EQ(errors[1], errors[0]);
			EXPECT_EQ(projectedSnapshot(applied), appliedState); // Failure before damage/consumption.
		}
		else
		{
			EXPECT_TRUE(errors[0].empty()); EXPECT_TRUE(errors[1].empty());
			EXPECT_TRUE(std::isfinite(values[0]));
			EXPECT_EQ(std::bit_cast<uint32_t>(values[0]), std::bit_cast<uint32_t>(values[1]));
			EXPECT_TRUE(applied->getPerfectFortuneState(BattleSide::DEFENDER).used);
		}
		EXPECT_EQ(projectedSnapshot(probe), probeState);
		EXPECT_EQ(nativeSnapshot(), native);
	}
	EXPECT_EQ(LIBRARY->scriptTypeHandler.get(), scriptsBefore);
	EXPECT_EQ(LIBRARY->identifiersHandler.get(), identifiersBefore);
}
