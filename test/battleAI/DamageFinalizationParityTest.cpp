/*
 * DamageFinalizationParityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/DamageCacheAdmission.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/battle/DamageCalculationTiming.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/battle/BattleLayout.h"
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

// Portable old/new characterization: pointer identities become packet IDs and
// floating fields retain their bits. Unordered bonus maps use sorted keys.
template<typename T> std::string traceValue(const T & value)
{
	if constexpr(std::is_same_v<T, const battle::Unit *>)
		return value ? std::to_string(value->unitId()) : "null";
	else if constexpr(std::is_same_v<T, BattleHex>)
		return std::to_string(value.toInt());
	else if constexpr(std::is_same_v<T, std::unordered_map<std::string, bool>>)
	{
		std::string result;
		for(const auto & [key, present] : std::map<std::string, bool>(value.begin(), value.end()))
			result += key + ':' + (present ? "1;" : "0;");
		return result;
	}
	else if constexpr(std::is_floating_point_v<T>)
		return ::testing::PrintToString(std::bit_cast<std::array<std::byte, sizeof(T)>>(value));
	else
		return ::testing::PrintToString(value);
}
struct PayloadTrace
{
	std::string text;
	template<typename T> void operator()(const char * name, T & value, const char *)
	{
		text += std::string(name) + '=' + traceValue(value) + '\n';
	}
};
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
	bool suppressHeroes = false;
	bool throwOnHeroQuery = false;
	mutable int heroQueries = 0;
	explicit FullLuckPolicyProxy(Subject subject) : BattleProxy(std::move(subject)) {}
	LuckRollRules getLuckRollRules() const override
	{
		++fullQueries;
		if(throwOnQuery)
			throw std::runtime_error("custom Luck rules query failed");
		return rules;
	}
	const CGHeroInstance * getSideHero(BattleSide side) const override
	{
		++heroQueries;
		if(throwOnHeroQuery)
			throw std::runtime_error("custom hero query failed");
		return suppressHeroes ? nullptr : BattleProxy::getSideHero(side);
	}

	// BattleProxy intentionally leaves mutation and a few read delegates
	// abstract. Forward reads; reject mutations so these callback tests cannot
	// accidentally simulate or repair state through their fault-injection wrapper.
	BattleID getBattleID() const override { return subject->getBattle()->getBattleID(); }
	const scripting::Pool & getScriptContextPool() const override { return subject->getBattle()->getScriptContextPool(); }
	std::vector<SpellID> getUsedSpells(BattleSide side) const override { return subject->getBattle()->getUsedSpells(side); }
	uint32_t nextUnitId() const override { return subject->getBattle()->nextUnitId(); }
	int64_t getActualDamage(const DamageRange & damage, int32_t count, vstd::RNG & rng) const override
	{
		return subject->getBattle()->getActualDamage(damage, count, rng);
	}
	int3 getLocation() const override { return subject->getBattle()->getLocation(); }
	BattleLayout getLayout() const override { return subject->getBattle()->getLayout(); }
	void nextRound() override { refuseMutation(); }
	void nextTurn(uint32_t, BattleUnitTurnReason) override { refuseMutation(); }
	void addUnit(uint32_t, const JsonNode &) override { refuseMutation(); }
	void updateUnit(uint32_t, const JsonNode &, int64_t) override { refuseMutation(); }
	void moveUnit(uint32_t, const BattleHex &) override { refuseMutation(); }
	void removeUnit(uint32_t) override { refuseMutation(); }
	void addUnitBonus(uint32_t, const std::vector<Bonus> &) override { refuseMutation(); }
	void updateUnitBonus(uint32_t, const std::vector<Bonus> &) override { refuseMutation(); }
	void removeUnitBonus(uint32_t, const std::vector<Bonus> &) override { refuseMutation(); }
	void setWallState(EWallPart, EWallState) override { refuseMutation(); }
	void addObstacle(const ObstacleChanges &) override { refuseMutation(); }
	void updateObstacle(const ObstacleChanges &) override { refuseMutation(); }
	void removeObstacle(uint32_t) override { refuseMutation(); }
private:
	[[noreturn]] static void refuseMutation() { throw std::logic_error("Read-only Luck policy callback cannot mutate battle"); }
};
static_assert(!std::is_abstract_v<FullLuckPolicyProxy>);

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

TEST_F(DamageFinalizationParityTest, OwnershipPayloadFollowsAllegianceAcrossLiveAndProjectedViews)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SecondarySkill armorer(SecondarySkill::decode("new-horizons:armorer"));
	attackerSideHero->setSecSkillLevel(armorer, 1, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setSecSkillLevel(armorer, 3, ChangeValueMode::ABSOLUTE);
	const int originalReduction = newHorizonsCombatSkills::armorerReductionPercent(attackerSideHero);
	const int transferredReduction = newHorizonsCombatSkills::armorerReductionPercent(defenderSideHero);
	ASSERT_NE(originalReduction, transferredReduction);
	ScopedCalculatorRegistry registry;
	auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary());
	registry.install(recorder);
	std::string trace;
	// Return to ordinary ownership too: successful call-local reads must not
	// survive a later call after control/bonus changes.
	for(int control : {0, 1, 2, 0})
	{
		std::shared_ptr<Bonus> marker;
		if(control == 1)
			marker = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
				BonusSource::OTHER, 1, BonusSourceID());
		else if(control == 2)
			marker = std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(
				SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID))), PlayerColor(1)));
		if(marker)
			braced->addNewBonus(marker);
		resetChild();
		// Rule resolution deliberately uses both actual heroes, while the public
		// player-facing getter still hides the opposing hero. Do not substitute it.
		ASSERT_EQ(child->getPlayerID(), PlayerColor(0));
		ASSERT_EQ(child->battleGetMySide(), BattleSide::ATTACKER);
		ASSERT_EQ(child->getSideHero(BattleSide::DEFENDER), defenderSideHero);
		ASSERT_EQ(child->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
		const auto authorized = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
		{
			auto exported = InitialExportBattleSubject::capture(authorized, *battle());
			auto projected = std::make_shared<HypotheticBattle>(environment.get(), exported);
			const std::array<std::shared_ptr<CBattleInfoCallback>, 3> callbacks{live, child, projected};
			for(size_t view = 0; view < callbacks.size(); ++view)
			{
				const auto & callback = *callbacks[view];
				const auto * actor = callback.battleGetUnitByID(incoming->unitId());
				const auto * target = callback.battleGetUnitByID(braced->unitId());
				ASSERT_NE(actor, nullptr);
				ASSERT_NE(target, nullptr);
				EXPECT_EQ(callback.battleGetOwner(target), PlayerColor(control == 1 ? 1 : 0));
				EXPECT_EQ(callback.battleGetActionController(target), PlayerColor(control ? 1 : 0));
				EXPECT_EQ(callback.battleGetOwnerHero(target),
					control == 1 ? defenderSideHero : attackerSideHero);
				const auto before = nativeSnapshot();
				const auto branchBefore = projectedSnapshot();
				const auto projectedActorBefore = actor->acquireState()->save();
				const auto projectedTargetBefore = target->acquireState()->save();
				const auto result = callback.calculateDmgRange(BattleAttackInfo(actor, target, 0, false));
				auto payload = recorder->payloads.back();
				EXPECT_EQ(payload.newHorizonsArmorerReductionPercent,
					control == 1 ? transferredReduction : originalReduction);
				PayloadTrace fields;
				payload.serializeScript(fields);
				trace += std::to_string(control) + '/' + std::to_string(view) + '\n' + fields.text;
				std::apply([&](const auto &... values) { ((trace += traceValue(values) + '\n'), ...); },
					allEstimationFields(result));
				EXPECT_EQ(nativeSnapshot(), before);
				EXPECT_EQ(projectedSnapshot(), branchBefore);
				EXPECT_EQ(actor->acquireState()->save(), projectedActorBefore);
				EXPECT_EQ(target->acquireState()->save(), projectedTargetBefore);
			}
		} // Retire the borrowed exported world before changing its native source.
		if(marker)
			braced->removeBonus(marker);
	}
	RecordProperty("ownershipPayloadTrace", trace);
}

TEST_F(DamageFinalizationParityTest, OwnershipQueriesRemainCallLocalIncludingNullHeroesAndFailures)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	FullLuckPolicyProxy proxy(live);
	proxy.rules = live->getBattle()->getLuckRollRules();
	const BattleAttackInfo attack(incoming, braced, 0, false);
	const auto before = nativeSnapshot();
	for(bool nullHeroes : {false, true, false})
	{
		proxy.suppressHeroes = nullHeroes;
		proxy.heroQueries = 0;
		const auto result = proxy.calculateDmgRange(attack);
		EXPECT_GE(result.damage.min, 0);
		EXPECT_GT(proxy.heroQueries, 0);
		RecordProperty(nullHeroes ? "nullHeroQueries" : "ordinaryHeroQueries", proxy.heroQueries);
	}
	proxy.throwOnHeroQuery = true;
	for(int attempt = 0; attempt < 2; ++attempt)
	{
		const int queries = proxy.heroQueries;
		EXPECT_THROW(proxy.calculateDmgRange(attack), std::runtime_error);
		EXPECT_EQ(proxy.heroQueries, queries + 1);
	}
	proxy.throwOnHeroQuery = false;
	EXPECT_NO_THROW(proxy.calculateDmgRange(attack));
	EXPECT_EQ(nativeSnapshot(), before);
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

// Pair-local prefix characterization uses the existing real-Lua recorder and
// complete payload/state/RNG oracles above. It also runs before production reuse.
class DamagePrefixReuseParityTest : public DamageFinalizationParityTest
{
protected:
	bool legacyElvenPrecision = false;
	bool finalLuckPolicy = true;
	void mapLoaded(CMap * loaded) override
	{
		DamageFinalizationParityTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		if(legacyElvenPrecision)
		{
			JsonNode registry(JsonPath::builtin("config/newHorizonsPerks"));
			registry.Struct().erase("sylvanTacticsPolicy");
			registry.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, registry);
		}
	}
	void selectLuckPerk(const char * skill, const char * perk)
	{
		const int decoded = SecondarySkill::decode(skill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({skill, perk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
	}

	BattleAttackInfo luckAttack(int luck, bool zeroChance = false, bool shooting = false, bool rebuild = true)
	{
		auto * source = shooting ? addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(94), 20) : braced;
		battle()->luckRollRules.goodChance = {zeroChance ? 0 : 25, zeroChance ? 0 : 50, zeroChance ? 0 : 75};
		battle()->luckRollRules.badChance = {zeroChance ? 0 : 25, zeroChance ? 0 : 50, zeroChance ? 0 : 75};
		battle()->luckRollRules.diceSize = 100;
		battle()->luckRollRules.affectsAllTargets = true;
		battle()->luckRollRules.finalDirectPhysicalMultiplier = finalLuckPolicy;
		if(rebuild) resetChild(); // Luck policy is captured by the real HB constructor.
		auto actor = child->getForUpdate(source->unitId());
		auto target = child->getForUpdate(incoming->unitId());
		actor->removeUnitBonus(Selector::type()(BonusType::LUCK));
		actor->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
			BonusType::LUCK, BonusSource::OTHER, luck, BonusSourceID())});
		for(const auto & unit : battle()->stacks)
			(void)unit->counterAttacks.available();
		return BattleAttackInfo(actor.get(), target.get(), 0, shooting);
	}
	void emitPair(const RecordingCalculator & recorder, int64_t value)
	{
		RecordProperty("expectedLuckDamage", std::to_string(value));
		std::string trace;
		for(auto payload : recorder.payloads)
		{
			PayloadTrace fields;
			payload.serializeScript(fields);
			trace += fields.text + "--next-payload--\n";
		}
		RecordProperty("completeNativePair", trace);
	}
};

TEST_F(DamagePrefixReuseParityTest, ZeroChanceStillRunsBothOrderedCanonicalCalculations)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1, true);
	ASSERT_GT(child->battleGetAttackLuck(attack.attacker, attack.defender, false), 0);
	ASSERT_EQ(child->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	ASSERT_EQ(child->getLuckRollRules().goodChance.front(), 0);
	const auto direct = child->calculateDmgRange(attack).damage;
	ScopedCalculatorRegistry registry;
	auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary());
	registry.install(recorder);
	const auto native = nativeSnapshot();
	const auto projected = projectedSnapshot();
	const auto value = child->battleExpectedLuckDamage(attack);
	EXPECT_EQ(value, direct.min + (direct.max - direct.min) / 2);
	ASSERT_EQ(recorder->events, "CC");
	ASSERT_EQ(recorder->payloads.size(), 2u);
	EXPECT_FALSE(recorder->payloads[0].luckyStrike);
	EXPECT_TRUE(recorder->payloads[1].luckyStrike);
	EXPECT_EQ(nativeSnapshot(), native);
	EXPECT_EQ(projectedSnapshot(), projected);
	EXPECT_EQ(child->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	emitPair(*recorder, value);
}

TEST_F(DamagePrefixReuseParityTest, NormalLuckyAndUnluckyKeepTheirEntirePayloadAndNativeState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1);
	ScopedCalculatorRegistry registry;
	auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary());
	registry.install(recorder);
	for(const auto & flags : std::array<std::pair<bool, bool>, 3>{{{false, false}, {true, false}, {false, true}}})
	{
		auto exact = attack;
		exact.luckyStrike = flags.first;
		exact.unluckyStrike = flags.second;
		ASSERT_NO_FATAL_FAILURE(parity(*child, exact, *recorder));
	}
	EXPECT_EQ(recorder->events, "CCCCCC");
	emitPair(*recorder, child->battleExpectedLuckDamage(attack));
}

class RepeatedNativeDamageParityTest : public DamagePrefixReuseParityTest
{
protected:
	void compareFullPair(const BattleAttackInfo & attack)
	{
		ASSERT_NE(child->battleGetFightingHero(BattleSide::ATTACKER), nullptr);
		ASSERT_EQ(child->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
		const auto projected = projectedSnapshot(); // Materialize canonical branch views before the immutable batch.
		ScopedCalculatorRegistry registry;
		auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary());
		registry.install(recorder);
		const auto native = nativeSnapshot();
		const auto expected = child->battleExpectedLuckDamage(attack);
		ASSERT_EQ(recorder->payloads.size(), 2u);
		damageCalculationTiming::Sample timing;
		int64_t actual = 0;
		{
			damageCalculationTiming::Binding bound(&timing);
			actual = child->battleExpectedLuckDamage(attack);
		}
		ASSERT_EQ(recorder->payloads.size(), 4u);
		EXPECT_EQ(actual, expected);
		expectPayload(recorder->payloads[0], recorder->payloads[2]);
		expectPayload(recorder->payloads[1], recorder->payloads[3]);
		EXPECT_EQ(recorder->events, "CCCC");
		EXPECT_EQ(timing.nativeAttackContext.calls, 2u);
		EXPECT_EQ(timing.nativePhysicalModifiers.calls, 2u);
		EXPECT_EQ(timing.nativeHeroOrders.calls, 2u);
		EXPECT_EQ(timing.nativeFinalization.calls, 2u);
		EXPECT_EQ(timing.bonusPreparation.calls, 2u);
		EXPECT_EQ(timing.luaCall.calls, 2u);
		EXPECT_EQ(nativeSnapshot(), native);
		EXPECT_EQ(projectedSnapshot(), projected);
		EXPECT_EQ(child->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
		emitPair(*recorder, actual);
	}
};

TEST_F(RepeatedNativeDamageParityTest, PlayerScopedHeroPairKeepsBothFullNativePayloads)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1);
	ASSERT_NO_FATAL_FAILURE(compareFullPair(attack));
}

TEST_F(DamagePrefixReuseParityTest, OrdinaryDamageNeverNeedsUnknownOrThrowingScriptIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1);
	(void)projectedSnapshot();
	const auto native = nativeSnapshot();
	const auto projected = projectedSnapshot();
	for(const auto capability : {Capability::UNKNOWN, Capability::QUERY_FAILURE, Capability::CANONICAL})
	{
		ScopedCalculatorRegistry registry;
		auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary(), capability);
		registry.install(recorder);
		const auto expected = child->battleExpectedLuckDamage(attack);
		damageCalculationTiming::Sample timing;
		{
			damageCalculationTiming::Binding bound(&timing);
			EXPECT_EQ(child->battleExpectedLuckDamage(attack), expected);
		}
		ASSERT_EQ(recorder->payloads.size(), 4u);
		expectPayload(recorder->payloads[0], recorder->payloads[2]);
		expectPayload(recorder->payloads[1], recorder->payloads[3]);
		EXPECT_EQ(recorder->events, "CCCC");
		EXPECT_EQ(timing.nativeAttackContext.calls, 2u);
		EXPECT_EQ(timing.luaCall.calls, 2u);
	}
	EXPECT_EQ(nativeSnapshot(), native);
	EXPECT_EQ(projectedSnapshot(), projected);
}

TEST_F(DamagePrefixReuseParityTest, CustomCallbackKeepsOrdinaryThrowingHeroQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto proxy = std::make_shared<FullLuckPolicyProxy>(live);
	proxy->rules = battle()->luckRollRules;
	child = std::make_shared<HypotheticBattle>(environment.get(), proxy);
	const auto attack = luckAttack(1, false, false, false);
	(void)projectedSnapshot();
	proxy->fullQueries = 0;
	proxy->heroQueries = 0;
	EXPECT_EQ(proxy->fullQueries, 0);
	EXPECT_EQ(proxy->heroQueries, 0);
	proxy->throwOnHeroQuery = true;
	EXPECT_THROW(child->battleExpectedLuckDamage(attack), std::runtime_error);
	EXPECT_GT(proxy->heroQueries, 0);
}

TEST_F(DamagePrefixReuseParityTest, ReboundEnvironmentRefusesCanonicalUnitQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1);
	(void)projectedSnapshot();
	auto actor = child->getForUpdate(attack.attacker->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	actor->bindStateEnvironment(sibling.get());
	const auto restore = vstd::makeScopeGuard([&]() { actor->bindStateEnvironment(child.get()); });
	EXPECT_FALSE(actor->usesStateEnvironment(*child));
	EXPECT_FALSE(actor->hasCanonicalDamageQuerySources(*child));
}

TEST_F(DamagePrefixReuseParityTest, CurrentLuckyAimKeepsItsLuckyOnlyFinalization)
{
	legacyElvenPrecision = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectLuckPerk("new-horizons:luck", "new-horizons:luck.luckyAim"));
	const auto attack = luckAttack(1, false, true);
	ScopedCalculatorRegistry registry;
	auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary());
	registry.install(recorder);
	const auto value = child->battleExpectedLuckDamage(attack);
	ASSERT_EQ(recorder->payloads.size(), 2u);
	EXPECT_EQ(recorder->payloads[0].luckyRangedDefenseIgnorePercent, 0);
	EXPECT_EQ(recorder->payloads[1].luckyRangedDefenseIgnorePercent, 25);
	emitPair(*recorder, value);
}

TEST_F(DamagePrefixReuseParityTest, HistoricalElvenPrecisionKeepsItsLuckyOnlyFinalization)
{
	legacyElvenPrecision = true;
	finalLuckPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectLuckPerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.elvenPrecision"));
	const auto attack = luckAttack(1, false, true);
	ScopedCalculatorRegistry registry;
	auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary());
	registry.install(recorder);
	const auto value = child->battleExpectedLuckDamage(attack);
	ASSERT_EQ(recorder->payloads.size(), 2u);
	EXPECT_EQ(recorder->payloads[0].luckyRangedDefenseIgnorePercent, 0);
	EXPECT_EQ(recorder->payloads[1].luckyRangedDefenseIgnorePercent, 25);
	emitPair(*recorder, value);
}

TEST_F(RepeatedNativeDamageParityTest, UnluckyPairKeepsAllPayloadBitsAndRounding)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(-1);
	ASSERT_NO_FATAL_FAILURE(compareFullPair(attack));
}

TEST_F(RepeatedNativeDamageParityTest, ZeroChancePairStillRunsBothFullNativeCalculations)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1, true);
	ASSERT_NO_FATAL_FAILURE(compareFullPair(attack));
}

TEST_F(RepeatedNativeDamageParityTest, LuckyAimPairReexecutesFlagDependentFinalizer)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectLuckPerk("new-horizons:luck", "new-horizons:luck.luckyAim"));
	const auto attack = luckAttack(1, false, true);
	ASSERT_NO_FATAL_FAILURE(compareFullPair(attack));
}

TEST_F(RepeatedNativeDamageParityTest, HistoricalElvenPairReexecutesFlagDependentFinalizer)
{
	legacyElvenPrecision = true;
	finalLuckPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectLuckPerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.elvenPrecision"));
	const auto attack = luckAttack(1, false, true);
	ASSERT_NO_FATAL_FAILURE(compareFullPair(attack));
}

namespace
{
class PrefixForeignLimiter final : public ILimiter
{
public:
	mutable unsigned calls = 0;
	EDecision limit(const BonusLimitationContext &) const override
	{
		++calls;
		throw std::runtime_error("foreign prefix limiter query");
	}
};
class PrefixForeignDamageCallback final : public CPlayerBattleCallback
{
public:
	mutable unsigned damageCalls = 0;
	using CPlayerBattleCallback::CPlayerBattleCallback;
	bool hasCanonicalDamageQuerySources(size_t remaining, const BattleInfo * expected) const override
	{
		(void)remaining;
		(void)expected;
		return true;
	}
	DamageEstimation calculateDmgRange(const BattleAttackInfo &) const override
	{
		++damageCalls;
		DamageEstimation result;
		result.damage = {40, 42};
		return result;
	}
};
class PrefixForeignGameState final : public CGameState {};
class PrefixForeignBattle final : public BattleInfo
{
public:
	PrefixForeignBattle() : BattleInfo(nullptr) {}
};
}

TEST_F(DamagePrefixReuseParityTest, ForeignModifierPreservesOrdinaryThrowingQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1);
	(void)projectedSnapshot();
	auto actor = child->getForUpdate(attack.attacker->unitId());
	auto limiter = std::make_shared<PrefixForeignLimiter>();
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE, BonusSource::OTHER, 1, BonusSourceID());
	bonus.limiter = limiter;
	ASSERT_EQ(actor->unitId(), braced->unitId());
	// Local projected additions intentionally do not evaluate limiters. Attach
	// to the inherited native source after snapshots, via its invalidating API.
	braced->addNewBonus(std::make_shared<Bonus>(bonus));
	EXPECT_EQ(limiter->calls, 0u);
	EXPECT_THROW(child->battleExpectedLuckDamage(attack), std::runtime_error);
	const auto once = limiter->calls;
	EXPECT_GT(once, 0u);
	EXPECT_THROW(child->battleExpectedLuckDamage(attack), std::runtime_error);
	EXPECT_EQ(limiter->calls, once + 1);
}

TEST_F(DamagePrefixReuseParityTest, ForeignDamageOverrideAndBattleSubclassKeepOrdinaryVirtualDispatch)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1);
	PrefixForeignDamageCallback callback(battle(), PlayerColor(0));
	EXPECT_TRUE(callback.hasCanonicalDamageQuerySources(512, nullptr));
	EXPECT_EQ(callback.battleExpectedLuckDamage(attack), 41);
	EXPECT_EQ(callback.damageCalls, 2u);
	PrefixForeignBattle foreign;
	CPlayerBattleCallback leaf(&foreign, PlayerColor(0));
	EXPECT_FALSE(leaf.hasCanonicalDamageQuerySources());
}

TEST_F(DamagePrefixReuseParityTest, NativeEnvironmentRebindingRefusesCanonicalUnitQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1);
	(void)projectedSnapshot();
	ASSERT_TRUE(braced->usesStateEnvironment(*braced));
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	braced->bindStateEnvironment(sibling.get());
	const auto restore = vstd::makeScopeGuard([&]() { braced->bindStateEnvironment(braced); });
	EXPECT_FALSE(child->getForUpdate(attack.attacker->unitId())->hasCanonicalDamageQuerySources(*child));
}

TEST_F(DamagePrefixReuseParityTest, ReboundCanonicalSubjectRefusesCanonicalQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1);
	(void)projectedSnapshot();
	auto newView = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto replacement = std::make_shared<HypotheticBattle>(environment.get(), newView);
	static_cast<BattleProxy &>(*child) = static_cast<const BattleProxy &>(*replacement);
	EXPECT_FALSE(child->hasCanonicalDamageQuerySources());
}

TEST_F(DamagePrefixReuseParityTest, NoSecondLuckCalculationRunsOneFullNativeCalculation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(0);
	auto actor = child->getForUpdate(attack.attacker->unitId());
	actor->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_EQ(child->battleGetAttackLuck(actor.get(), attack.defender, false), 0);
	ScopedCalculatorRegistry registry;
	auto recorder = std::make_shared<RecordingCalculator>(registry.ordinary(), Capability::QUERY_FAILURE);
	registry.install(recorder);
	damageCalculationTiming::Sample timing;
	{
		damageCalculationTiming::Binding bound(&timing);
		(void)child->battleExpectedLuckDamage(attack);
	}
	EXPECT_EQ(recorder->events, "C");
	EXPECT_EQ(timing.nativeAttackContext.calls, 1u);
	EXPECT_EQ(timing.nativeFinalization.calls, 1u);
	EXPECT_EQ(timing.luaCall.calls, 1u);
}

TEST_F(DamagePrefixReuseParityTest, ForeignGameContextCannotHideBehindExactNativeBattleType)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	PrefixForeignGameState foreign;
	auto * original = battle()->cb;
	battle()->cb = &foreign;
	const auto restore = vstd::makeScopeGuard([&]() { battle()->cb = original; });
	EXPECT_FALSE(child->hasCanonicalDamageQuerySources());
}

TEST_F(DamagePrefixReuseParityTest, NonHypotheticMainCacheKeepsCustomVirtualDamageFallback)
{
	useRevisedOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attack = luckAttack(1);
	auto callback = std::make_shared<PrefixForeignDamageCallback>(battle(), PlayerColor(0));
	ASSERT_TRUE(callback->hasCanonicalDamageQuerySources(512, nullptr));
	const CBattleInfoCallback * callbackBase = callback.get();
	ASSERT_EQ(dynamic_cast<const HypotheticBattle *>(callbackBase), nullptr);
	const auto native = nativeSnapshot();
	const auto projected = projectedSnapshot();
	DamageCache cache;
	DamageCacheAdmission admission(*attack.attacker, *callback);
	ASSERT_TRUE(admission.matches(*attack.attacker, *callback));
	DamageCacheProbeTiming timing;
	EXPECT_EQ(cache.getDamage(attack.attacker, attack.defender, callback, &timing, &admission), 41);
	EXPECT_EQ(callback->damageCalls, 2u);
	EXPECT_EQ(timing.dynamicBypasses, 1u);
	EXPECT_EQ(nativeSnapshot(), native);
	EXPECT_EQ(projectedSnapshot(), projected);
}
