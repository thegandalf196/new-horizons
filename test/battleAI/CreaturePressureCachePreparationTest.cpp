/*
 * CreaturePressureCachePreparationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/ArrowContinuationReuse.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/ExchangeInitiativeQueue.h"
#include "../../AI/BattleAI/DamageCacheAdmission.h"
#include "../../AI/BattleAI/AttackPreviewHitContext.h"
#include "../../AI/BattleAI/NewHorizonsHexOfPain.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include <bit>
#include <sstream>
#include "../../lib/GameLibrary.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/combatScripts/NativeDamageCalculator.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/scripting/ScriptHandler.h"
#include <functional>

namespace
{
class ArrowQueryFailureEffect final : public spells::effects::Effect
{
	const spells::effects::Effect & delegate;
public:
	mutable unsigned queries = 0;
	explicit ArrowQueryFailureEffect(const spells::effects::Effect & delegate) : delegate(delegate) {}
	spells::effects::ForecastImplementation forecastImplementation(const spells::Mechanics *) const override
	{ ++queries; throw std::runtime_error("fixture optional effect-query failure"); }
	void adjustAffectedHexes(BattleHexArray & h, const spells::Mechanics * m, const spells::Target & t) const override
	{ delegate.adjustAffectedHexes(h, m, t); }
	void apply(ServerCallback * s, const spells::Mechanics * m, const spells::Target & t) const override
	{ delegate.apply(s, m, t); }
	spells::Target filterTarget(const spells::Mechanics * m, const spells::Target & t) const override
	{ return delegate.filterTarget(m, t); }
	spells::Target transformTarget(const spells::Mechanics * m, const spells::Target & a, const spells::Target & t) const override
	{ return delegate.transformTarget(m, a, t); }
protected:
	void initImpl(JsonNode) override {}
};
class ArrowUnknownUpdater final : public IUpdater {};

using Sample = damageCalculationTiming::Sample;
enum class Capability { CANONICAL, UNKNOWN, REVOKED_AFTER_CALCULATION, QUERY_FAILURE, CALCULATION_FAILURE, REVOKED_AFTER_ADMISSION };

class RecordingCalculator final : public IDamageCalculatorScript
{
	const IDamageCalculatorScript * delegate;
	Capability capability;
	mutable bool revoked = false;
	mutable bool admittedOnce = false;
public:
	mutable std::vector<std::pair<uint32_t, uint32_t>> calls;
	mutable std::string events;
	mutable std::vector<std::pair<size_t, DamageForecastImplementation>> capabilityReturns;
	std::optional<uint32_t> admissionActor;
	std::function<void(const CBattleInfoCallback &, const DamageAttackInfo &, const DamageEstimation &)> observe;
	explicit RecordingCalculator(const IDamageCalculatorScript * delegate, Capability capability)
		: delegate(delegate), capability(capability) {}
	void reset() { calls.clear(); events.clear(); capabilityReturns.clear(); revoked = false; admittedOnce = false; }
	DamageEstimation calculate(const CBattleInfoCallback & battle, DamageAttackInfo & info) const override
	{
		calls.emplace_back(info.attacker->unitId(), info.defender->unitId());
		events += 'C';
		if(capability == Capability::CALCULATION_FAILURE)
			throw std::runtime_error("fixture calculation failure");
		const auto answer = delegate->calculate(battle, info);
		if(observe) observe(battle, info, answer);
		revoked = capability == Capability::REVOKED_AFTER_CALCULATION;
		return answer;
	}
	DamageForecastImplementation forecastImplementation(const CBattleInfoCallback & battle) const override
	{
		events += 'Q';
		const auto recordReturn = [&](DamageForecastImplementation result)
		{
			capabilityReturns.emplace_back(events.size() - 1, result);
			return result;
		};
		if(admissionActor && (calls.empty() || calls.back().first != *admissionActor))
			return recordReturn(DamageForecastImplementation::UNKNOWN);
		if(capability == Capability::QUERY_FAILURE)
			throw std::runtime_error("fixture capability-query failure");
		if(capability == Capability::REVOKED_AFTER_ADMISSION)
		{
			if(admittedOnce) return recordReturn(DamageForecastImplementation::UNKNOWN);
			admittedOnce = true;
		}
		return recordReturn(capability == Capability::UNKNOWN || revoked ? DamageForecastImplementation::UNKNOWN
			: delegate->forecastImplementation(battle));
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

void expectCalculations(const Sample & sample, uint64_t expected, size_t recordedCalculations)
{
	EXPECT_EQ(recordedCalculations, expected);
	EXPECT_EQ(sample.initialCalls, expected);
	EXPECT_EQ(sample.nativePayload.calls, expected);
	// These two scopes belong exclusively to the optional Lua oracle bridge.
	// Native work remains observed by payload counts and the real calculator.
	EXPECT_EQ(sample.bonusPreparation.calls, 0u);
	EXPECT_EQ(sample.bonusPreparation.nanoseconds, 0u);
	EXPECT_EQ(sample.luaCall.calls, 0u);
	EXPECT_EQ(sample.luaCall.nanoseconds, 0u);
	EXPECT_EQ(sample.luckySecondCalls, 0u);
	EXPECT_EQ(sample.unluckySecondCalls, 0u);
	EXPECT_EQ(sample.suppressedStages, 0u);
}

// This fixture has at most two opposing Pikemen. Only the selected duplicate
// after TWO already-matched actual probes may disappear; no general diff mask.
void expectOnlySelectedDuplicatesRemoved(const std::vector<JsonNode> & reference,
	const std::vector<JsonNode> & candidate, const std::vector<uint32_t> & ids, uint64_t uses)
{
	size_t original = 0, current = 0;
	uint64_t removed = 0;
	while(original < reference.size())
	{
		if(current < candidate.size() && reference[original] == candidate[current])
		{
			++original;
			++current;
			continue;
		}
		ASSERT_GE(original, 2u);
		ASSERT_GE(current, 2u);
		const auto & first = reference[original - 2];
		const auto & second = reference[original - 1];
		const auto & applied = reference[original];
		ASSERT_EQ(first, candidate[current - 2]);
		ASSERT_EQ(second, candidate[current - 1]);
		ASSERT_EQ(first["actor"], second["actor"]);
		ASSERT_EQ(first["units"], second["units"]);
		ASSERT_NE(first["target"], second["target"]);
		const auto score = [](const JsonNode & row)
		{ return std::bit_cast<float>(static_cast<uint32_t>(row["ordinaryScoreBits"].Integer())); };
		const auto & winner = score(second) > score(first) ? second : first;
		ASSERT_EQ(applied, winner); // First strict maximum, including first-in-order ties.
		ASSERT_GT(applied["healthLoss"].Integer(), 0);
		if(original + 1 < reference.size())
		{
			ASSERT_LT(current, candidate.size());
			const auto & after = reference[original + 1];
			ASSERT_EQ(after, candidate[current]);
			for(size_t index = 0; index < ids.size(); ++index)
			{
				const auto loss = ids[index] == applied["target"].Integer() ? applied["healthLoss"].Integer() : 0;
				EXPECT_EQ(after["health"].Vector()[index].Integer(), applied["health"].Vector()[index].Integer() - loss);
			}
		}
		else
			EXPECT_EQ(current, candidate.size()); // No later callback: no terminal-health claim.
		++removed;
		++original;
	}
	EXPECT_EQ(current, candidate.size());
	EXPECT_EQ(removed, uses);
}

}

class CreaturePressureCachePreparationTest : public HeroCommandFixture
{
protected:
	bool legacyCommandRules = false;
	bool selectedPreemptive = false;
	std::shared_ptr<projectedHitTest::Environment> environment;
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
		if(selectedPreemptive)
		{
			const std::string skill = "new-horizons:battlecraft";
			const SecondarySkill id(SecondarySkill::decode(skill));
			for(const auto & [rank, perk] : {
				std::pair{MasteryLevel::BASIC, "new-horizons:battlecraft.entrench"},
				std::pair{MasteryLevel::ADVANCED, "new-horizons:battlecraft.preEmptiveStrike"}})
			{
				attackerSideHero->setSecSkillLevel(id, rank, ChangeValueMode::ABSOLUTE);
				ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(skill, perk,
					attackerSideHero->getPerkSkillRank(skill)));
				attackerSideHero->applyPerkSelection({skill, perk});
				ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
			}
		}
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
		environment = std::make_shared<projectedHitTest::Environment>(gameState());
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
			ASSERT_NE(dynamic_cast<const NativeDamageCalculator *>(registry.ordinary()), nullptr);
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
			expectCalculations(ordinary, 8, calls.size());
			expectCalculations(optimized, fast ? 2 : 8, calculator->calls.size());
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
	void compareSelectedExchange(bool laterLoser, Capability candidateCapability = Capability::CANONICAL, bool laterWinner = false, bool subsequent = false)
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(prepare());
		ASSERT_TRUE(heroCommands::isRevisedRules(model->getHeroCommandRules()));
		// Opening AP strikes the OTHER enemy and blocks retaliation, leaving the
		// intended selector actor and both targets untouched before its activation.
		model->getForUpdate(ids[1])->position = BattleHex(4, 2);
		model->getForUpdate(ids[2])->position = BattleHex(4, 3);
		model->getForUpdate(ids[3])->position = BattleHex(3, 4);
		for(const auto id : ids)
			model->getForUpdate(id)->addUnitBonus({
				Bonus(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()),
				Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID())});
		for(const auto [index, speed] : {std::pair{0, 30}, std::pair{2, 20}, std::pair{1, 10}})
			model->getForUpdate(ids[index])->addUnitBonus({
				Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED, BonusSource::OTHER, speed, BonusSourceID())});
		for(const auto target : {ids[0], ids[1]})
			ASSERT_TRUE(model->isMeleeAttackPossible(model->battleGetUnitByID(ids[2]), model->battleGetUnitByID(target)));
		ASSERT_TRUE(model->isMeleeAttackPossible(model->battleGetUnitByID(ids[0]), model->battleGetUnitByID(ids[3])));
		if(laterLoser)
		{
			Bonus protection(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
				BonusSource::OTHER, 80, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::damageTypeAll));
			// Actual first participant ids[1]; protect it only for the later-winner case.
			model->getForUpdate(ids[laterWinner ? 1 : 0])->addUnitBonus({protection});
		}
		if(subsequent)
		{
			model->getForUpdate(ids[2])->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
				BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID())});
			// STACK_ACTIVATION survives activation start. Preserve the producer's
			// exact identity; the first applied strike owns its native consumption.
			model->getForUpdate(ids[2])->addUnitBonus(newHorizonsShroud::nightProwlerDamageBonuses());
			ASSERT_EQ(AttackPossibility::getAttackCount(*model->battleGetUnitByID(ids[2]), false, *model), 2);
		}
		std::vector<int64_t> initialHealth;
		for(const auto id : ids) initialHealth.push_back(model->battleGetUnitByID(id)->getAvailableHealth());
		const auto liveBefore = gameState()->saveToMemory();
		const auto nativeBefore = projectedHitTest::serialized(*battle());
		const auto projectedBefore = projectedStates();
		const auto serverBefore = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameBefore = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto defaultBefore = projectedHitTest::serialized(CRandomGenerator::getDefault());
		std::optional<uint32_t> expected;
		std::optional<std::pair<uint64_t, uint64_t>> expectedWork;
		std::optional<std::vector<JsonNode>> expectedLedger;
		uint64_t referenceCalculations = 0;
		for(const auto capability : {Capability::UNKNOWN, candidateCapability})
		{
			ScopedCalculatorRegistry registry;
			ASSERT_NE(registry.ordinary(), nullptr);
			ASSERT_EQ(registry.ordinary()->forecastImplementation(*model), DamageForecastImplementation::CANONICAL);
			auto calculator = std::make_shared<RecordingCalculator>(registry.ordinary(), capability);
			registry.install(calculator);
			DamageCache original;
			original.buildDamageCache(model, BattleSide::ATTACKER);
			DamageCache damage(&original);
			damage.buildDamageCache(model, BattleSide::ATTACKER);
			const auto parentBefore = original.captureSnapshot();
			const auto opening = model->getForUpdate(ids[0]);
			PotentialTargets targets(opening.get(), damage, model);
			const auto attack = AttackPossibility::evaluate(BattleAttackInfo(opening.get(), model->battleGetUnitByID(ids[3]), 0, false),
				opening->getPosition(), damage, model);
			BattleExchangeEvaluator evaluator(model, environment, 1.0f, 2);
			evaluator.updateReachabilityMap(model);
			// Membership preserves canonical round order; army append retains each
			// participant's first appearance, rather than sorting by stack ID.
			const auto membership = evaluator.getExchangeUnits(attack, 0, targets, model);
			battle::Units ours, enemies{attack.attack.defender};
			ExchangeInitiativeQueue::appendParticipantArmies(*model, opening.get(), membership.units, ours, enemies);
			ASSERT_EQ(ours.size(), 2u);
			ASSERT_EQ(ours[0]->unitId(), ids[1]);
			ASSERT_EQ(ours[1]->unitId(), ids[0]);
			calculator->reset();
			struct Observation
			{
				const CBattleInfoCallback * branch;
				uint32_t actor, target;
				int64_t damage;
				int64_t healthLoss;
				float ordinaryScore;
				bool contact;
				std::vector<JsonNode> states;
				std::vector<int64_t> health;
				bool nightProwler = false;
			};
			std::vector<Observation> observed;
			calculator->observe = [&](const CBattleInfoCallback & actual, const DamageAttackInfo & info, const DamageEstimation & result)
			{
				Observation row{&actual, info.attacker->unitId(), info.defender->unitId(),
					result.damage.min + (result.damage.max - result.damage.min) / 2, 0, 0, actual.isMeleeAttackPossible(info.attacker, info.defender), {}, {}};
				for(const auto id : ids)
				{
					const auto * unit = dynamic_cast<const StackWithBonuses *>(actual.battleGetUnitByID(id));
					ASSERT_NE(unit, nullptr);
					row.states.push_back(unit->acquireState()->save());
					row.health.push_back(unit->getAvailableHealth());
				}
				// Ordinary Pikemen: no retaliation or secondary effects. Reprice the
				// actual child's raw hit using the existing pure score routine. The
				// fully prepared parent's original pair is already present: no query
				// back into this calculator, candidate policy or gameplay mutation.
				row.nightProwler = actual.battleGetUnitByID(ids[2])->hasBonus(
					CSelector(newHorizonsShroud::isNightProwlerBonus));
				const auto callsBefore = calculator->calls.size();
				auto view = std::shared_ptr<CBattleInfoCallback>(const_cast<CBattleInfoCallback *>(&actual), [](auto *) {});
				BattleAttackInfo hit(info.attacker, info.defender, 0, info.shooting);
				const auto lastStand = AttackPreviewHitContext::resolveUnspentLastStand(actual, hit, info.defender, row.damage);
				const auto projection = battleAIProjectDamage(info.defender, lastStand.first.damageToApply,
					battleAIDamageProvenance(info.attacker, hit.physicalDamage));
				row.healthLoss = projection.healthLoss;
				DamageCacheProbeTiming scoreTiming;
				row.ordinaryScore = AttackPossibility::calculateDamageReduce(info.attacker, info.defender,
					projection.healthLoss, damage, view, &scoreTiming);
				EXPECT_EQ(scoreTiming.parentOriginalHits, 1u);
				EXPECT_EQ(scoreTiming.getDamageCalls, 0u);
				EXPECT_EQ(scoreTiming.parentOriginalFallbacks, 0u);
				// These are the only ordinary-probe post-hit score riders.
				EXPECT_FALSE(newHorizonsHexOfPainAI::hasEffect(info.attacker));
				EXPECT_FALSE(actual.battleCanTriggerCleave(info.attacker));
				EXPECT_EQ(info.attacker->creatureId(), creatureByName("core:pikeman"));
				EXPECT_TRUE(info.attacker->hasBonusOfType(BonusType::BLOCKS_RETALIATION));
				EXPECT_EQ(actual.battleGetAttackLuck(info.attacker, info.defender, info.shooting), 0);
				EXPECT_EQ(calculator->calls.size(), callsBefore);
				observed.push_back(std::move(row));
			};
			BattleMovementTiming::ExchangeTiming timing;
			const auto score = evaluator.evaluateExchange(attack, 0, targets, damage, model, &timing);
			calculator->observe = {};
			const auto first = std::find_if(observed.begin(), observed.end(), [&](const Observation & row) { return row.actor == ids[2]; });
			ASSERT_NE(first, observed.end());
			const auto index = static_cast<size_t>(first - observed.begin());
			ASSERT_LT(index + 3, observed.size());
			const auto & probe0 = observed[index];
			const auto & probe1 = observed[index + 1];
			const bool optimized = capability == Capability::CANONICAL;
			const auto & winner = probe1.ordinaryScore > probe0.ordinaryScore ? probe1 : probe0;
			const auto & selected = optimized ? winner : observed[index + 2];
			const auto & after = observed[index + (optimized ? 2 : 3)];
			// Actual child probes, never probes on the earlier parent. UNKNOWN
			// records the selected application; reuse removes only that query.
			// Either next callback must observe the identical selected health loss.
			for(const auto * row : {&probe0, &probe1, &selected})
			{
				EXPECT_EQ(row->actor, ids[2]);
				EXPECT_TRUE(row->contact);
				for(const auto health : row->health) EXPECT_GT(health, 0);
				EXPECT_EQ(row->branch, probe0.branch);
				EXPECT_NE(row->branch, model.get());
				EXPECT_EQ(row->states, probe0.states);
				EXPECT_EQ(row->health[0], initialHealth[0]);
				EXPECT_EQ(row->health[1], initialHealth[1]);
				EXPECT_EQ(row->health[2], initialHealth[2]);
			}
			EXPECT_EQ(probe0.target, ids[1]);
			EXPECT_EQ(probe1.target, ids[0]);
			if(laterWinner)
			{
				EXPECT_LT(probe0.ordinaryScore, probe1.ordinaryScore);
				EXPECT_EQ(selected.target, ids[0]);
				EXPECT_EQ(selected.damage, probe1.damage);
				EXPECT_GT(selected.healthLoss, 0);
				EXPECT_EQ(after.branch, probe0.branch);
				EXPECT_EQ(after.health[0], selected.health[0] - selected.healthLoss);
				EXPECT_EQ(after.health[1], selected.health[1]);
			}
			else
			{
				EXPECT_EQ(selected.target, ids[1]);
				if(laterLoser) EXPECT_GT(probe0.ordinaryScore, probe1.ordinaryScore);
				else EXPECT_EQ(std::bit_cast<uint32_t>(probe0.ordinaryScore), std::bit_cast<uint32_t>(probe1.ordinaryScore));
				EXPECT_EQ(selected.damage, probe0.damage);
				EXPECT_GT(selected.healthLoss, 0);
				EXPECT_EQ(after.branch, probe0.branch);
				EXPECT_EQ(after.health[1], selected.health[1] - selected.healthLoss);
				EXPECT_EQ(after.health[0], selected.health[0]);
			}
			if(subsequent)
			{
				// The next callback is the SECOND ordinary blow in this activation.
				// It must retain its live lookup after the first receipt is spent.
				EXPECT_EQ(after.actor, ids[2]);
				EXPECT_EQ(after.target, selected.target);
				EXPECT_TRUE(probe0.nightProwler);
				EXPECT_TRUE(probe1.nightProwler);
				EXPECT_TRUE(selected.nightProwler);
				EXPECT_FALSE(after.nightProwler); // Native aftermath precedes the second lookup.
				EXPECT_LT(after.damage, selected.damage); // Genuine +10% marker was removed, not a stale receipt.
				const auto marker = CSelector(newHorizonsShroud::isNightProwlerBonus);
				EXPECT_TRUE(model->getForUpdate(ids[2])->hasBonus(marker));
				ASSERT_LT(index + (optimized ? 3 : 4), observed.size());
				const auto & afterSecond = observed[index + (optimized ? 3 : 4)];
				EXPECT_FALSE(afterSecond.nightProwler);
				const auto victim = selected.target == ids[0] ? 0u : 1u;
				EXPECT_EQ(afterSecond.health[victim], after.health[victim] - after.healthLoss);
			}
			const auto observedLabel = capability == Capability::UNKNOWN ? "Unknown" : "Candidate";
			RecordProperty(std::string(observedLabel) + "ActualFirstProbeScoreBits", std::to_string(std::bit_cast<uint32_t>(probe0.ordinaryScore)));
			RecordProperty(std::string(observedLabel) + "ActualSecondProbeScoreBits", std::to_string(std::bit_cast<uint32_t>(probe1.ordinaryScore)));
			RecordProperty(std::string(observedLabel) + "SelectedTargetByExactHealthLoss", std::to_string(selected.target));
			std::vector<JsonNode> ledger;
			for(const auto & row : observed)
			{
				JsonNode entry;
				entry["actor"].Integer() = row.actor;
				entry["target"].Integer() = row.target;
				entry["rawDamage"].Integer() = row.damage;
				entry["healthLoss"].Integer() = row.healthLoss;
				entry["ordinaryScoreBits"].Integer() = std::bit_cast<uint32_t>(row.ordinaryScore);
				entry["nightProwler"].Bool() = row.nightProwler;
				entry["units"].Vector() = row.states;
				for(const auto health : row.health)
				{
					JsonNode value;
					value.Integer() = health;
					entry["health"].Vector().push_back(std::move(value));
				}
				ledger.push_back(std::move(entry));
			}
			if(!expectedLedger)
			{
				expectedLedger = ledger;
				referenceCalculations = calculator->calls.size();
				EXPECT_EQ(timing.selectedProbeDamageOffers, 0u);
				EXPECT_EQ(timing.selectedProbeDamageUses, 0u);
				EXPECT_EQ(timing.selectedProbeScoreUses, 0u);
			}
			else if(optimized)
			{
				ASSERT_GT(timing.selectedProbeDamageUses, 0u);
				EXPECT_EQ(timing.selectedProbeScoreUses, timing.selectedProbeDamageUses);
				EXPECT_GE(timing.selectedProbeDamageOffers, timing.selectedProbeDamageUses);
				ASSERT_GE(referenceCalculations, calculator->calls.size());
				EXPECT_EQ(referenceCalculations - calculator->calls.size(), timing.selectedProbeDamageUses);
				ASSERT_NO_FATAL_FAILURE(expectOnlySelectedDuplicatesRemoved(*expectedLedger, ledger, ids, timing.selectedProbeDamageUses));
			}
			else
			{
				EXPECT_EQ(ledger, *expectedLedger);
				EXPECT_EQ(calculator->calls.size(), referenceCalculations);
				EXPECT_EQ(timing.selectedProbeDamageUses, 0u);
				EXPECT_EQ(timing.selectedProbeScoreUses, 0u);
				if(capability == Capability::REVOKED_AFTER_ADMISSION)
					EXPECT_GT(timing.selectedProbeDamageOffers, 0u); // Admitted first, revoked before consume.
				else EXPECT_EQ(timing.selectedProbeDamageOffers, 0u);
			}
			ASSERT_FALSE(calculator->events.empty());
			EXPECT_EQ(calculator->events.front(), 'C'); // Preserve ordinary bootstrap before capability query.
			ASSERT_GT(timing.targetProbes.calls, 0u);
			ASSERT_GT(timing.appliedAttacks.calls, 0u);
			ASSERT_GT(calculator->calls.size(), 0u);
			const auto work = std::pair{timing.targetProbes.calls, timing.appliedAttacks.calls};
			if(!expectedWork) expectedWork = work;
			else EXPECT_EQ(work, *expectedWork); // No capability-dependent consumer/candidate policy confound.
			const auto bits = std::bit_cast<uint32_t>(score);
			if(!expected) expected = bits;
			else EXPECT_EQ(bits, *expected); // Capability-only wrapping changes no score or candidate policy here.
			const auto label = capability == Capability::UNKNOWN ? "Unknown" : "Candidate";
			RecordProperty(std::string(label) + "ExchangeScoreBits", std::to_string(bits));
			RecordProperty(std::string(label) + "Calculations", std::to_string(calculator->calls.size()));
			RecordProperty(std::string(label) + "TargetProbes", std::to_string(timing.targetProbes.calls));
			RecordProperty(std::string(label) + "AppliedAttacks", std::to_string(timing.appliedAttacks.calls));
			RecordProperty(std::string(label) + "SelectedProbeOffers", std::to_string(timing.selectedProbeDamageOffers));
			RecordProperty(std::string(label) + "SelectedProbeUses", std::to_string(timing.selectedProbeDamageUses));
			std::ostringstream trace;
			for(const auto & [attacker, defender] : calculator->calls)
				trace << attacker << ',' << defender << ';';
			RecordProperty(std::string(label) + "CalculationPairs", trace.str());
			EXPECT_TRUE(original.matchesSnapshot(parentBefore));
			EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
			EXPECT_EQ(projectedHitTest::serialized(*battle()), nativeBefore);
			EXPECT_EQ(projectedStates(), projectedBefore);
			EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverBefore);
			EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameBefore);
			EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultBefore);
		}
	}
	void compareSelectedFallback(bool specialized)
	{
		selectedPreemptive = specialized;
		useRevisedOrders = specialized;
		ASSERT_NO_FATAL_FAILURE(prepare());
		// BattleHex(3,3)->(4,2) has distance TWO. This triangle makes BOTH
		// opposing targets contactable and the native Protect pair adjacent.
		for(const auto & [index, hex] : {std::pair{0u, BattleHex(3, 3)}, std::pair{1u, BattleHex(3, 2)},
			std::pair{2u, BattleHex(4, 3)}, std::pair{3u, BattleHex(3, 4)}})
			battle()->getStack(ids[index], false)->position = hex;
		if(!specialized)
		{
			// Native active-unit priority is part of public round0 membership.
			// Establish the SAME action phase before deriving the ward and issuing.
			BattleSetActiveStack active;
			active.battleID = BattleID(0); active.stack = ids[0];
			active.reason = BattleUnitTurnReason::TURN_QUEUE;
			gameHandler->sendAndApply(active);
		}
		const auto reset = [&]()
		{
			auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
			model = std::make_shared<HypotheticBattle>(environment.get(), callback);
			for(const auto id : ids)
				model->getForUpdate(id)->addUnitBonus({
					Bonus(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()),
					Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID())});
			for(const auto [index, speed] : {std::pair{0, 30}, std::pair{2, 20}, std::pair{1, 10}})
				model->getForUpdate(ids[index])->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
					BonusType::STACKS_SPEED, BonusSource::OTHER, speed, BonusSourceID())});
		};
		reset();
		if(!specialized) ASSERT_EQ(model->battleActiveUnit()->unitId(), ids[0]);
		uint32_t ward = ids[1], protector = ids[0];
		if(!specialized)
		{
			ASSERT_EQ(model->getHeroCommandRules()["rulesetVersion"].Integer(), 3);
			ASSERT_TRUE(heroCommands::supportedByRules(model->getHeroCommandRules(), HeroCommand::PROTECT));
			// Derive the first actual candidate BEFORE issuing the native Order;
			// protect that candidate, then require this order to survive issuance.
			{
				DamageCache setupCache;
				setupCache.buildDamageCache(model, BattleSide::ATTACKER);
				const auto opener = model->getForUpdate(ids[0]);
				PotentialTargets setupTargets(opener.get(), setupCache, model);
				const auto opening = AttackPossibility::evaluate(BattleAttackInfo(opener.get(),
					model->battleGetUnitByID(ids[3]), 0, false), opener->getPosition(), setupCache, model);
				BattleExchangeEvaluator setup(model, environment, 1.0f, 1);
				setup.updateReachabilityMap(model);
				const auto participants = setup.getExchangeUnits(opening, 0, setupTargets, model);
				battle::Units ours, enemies{opening.attack.defender};
				ExchangeInitiativeQueue::appendParticipantArmies(*model, opener.get(), participants.units, ours, enemies);
				ASSERT_EQ(ours.size(), 2u);
				ward = ours.front()->unitId();
				protector = ours.back()->unitId();
			}
			ASSERT_EQ(BattleHex::getDistance(battle()->battleGetUnitByID(ward)->getPosition(),
				battle()->battleGetUnitByID(protector)->getPosition()), 1);
			ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT));
			ASSERT_TRUE(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
				HeroCommand::PROTECT, {protector, ward}).has_value());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
				BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT, protector, ward)));
			reset();
			ASSERT_EQ(model->battleActiveUnit()->unitId(), ids[0]);
			ASSERT_EQ(model->battleResolveHeroOrderTarget(model->battleGetUnitByID(ids[2]),
				model->battleGetUnitByID(ward), false)->unitId(), protector);
		}
		else
		{
			model->getForUpdate(ids[1])->defending = true;
			// The defended unit is absent from round0's active queue, so it enters
			// the public candidate membership AFTER the ordinary opening unit.
			// Make that first ordinary target harmless, not the specialized action.
			model->getForUpdate(ids[0])->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
				BonusType::GENERAL_DAMAGE_REDUCTION, BonusSource::OTHER, 100, BonusSourceID(),
				BonusSubtypeID(BonusCustomSubtype::damageTypeAll))});
			model->getForUpdate(ids[2])->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
				BonusType::PRIMARY_SKILL, BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK))});
			ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
				model->battleGetOwnerHero(model->battleGetUnitByID(ids[1])),
				model->battleGetUnitByID(ids[1]), model->battleGetRound()), 50);
		}
		for(const auto id : {ids[0], ids[1]})
			ASSERT_TRUE(model->isMeleeAttackPossible(model->battleGetUnitByID(ids[2]), model->battleGetUnitByID(id)));
		ASSERT_TRUE(heroCommands::supportedByRules(model->getHeroCommandRules(), HeroCommand::FOCUS_FIRE));
		const auto world = gameState()->saveToMemory(), native = projectedHitTest::serialized(*battle());
		const auto units = projectedStates();
		const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
		std::optional<std::vector<JsonNode>> expected;
		std::optional<uint32_t> scoreBits;
		std::optional<std::pair<uint64_t, uint64_t>> work;
		for(const auto capability : {Capability::UNKNOWN, Capability::CANONICAL})
		{
			ScopedCalculatorRegistry registry;
			ASSERT_NE(registry.ordinary(), nullptr);
			auto calculator = std::make_shared<RecordingCalculator>(registry.ordinary(), capability);
			// Other actors remain UNKNOWN: isolate the two-target boundary actor's
			// no-receipt path without masking an unrelated eligible reuse elsewhere.
			calculator->admissionActor = ids[2];
			registry.install(calculator);
			DamageCache original;
			original.buildDamageCache(model, BattleSide::ATTACKER);
			DamageCache cache(&original);
			cache.buildDamageCache(model, BattleSide::ATTACKER);
			const auto parentBefore = original.captureSnapshot();
			const auto opening = model->getForUpdate(ids[0]);
			PotentialTargets targets(opening.get(), cache, model);
			const auto attack = AttackPossibility::evaluate(BattleAttackInfo(opening.get(),
				model->battleGetUnitByID(ids[3]), 0, false), opening->getPosition(), cache, model);
			BattleExchangeEvaluator evaluator(model, environment, 1.0f, 1);
			evaluator.updateReachabilityMap(model);
			const auto membership = evaluator.getExchangeUnits(attack, 0, targets, model);
			battle::Units ours, enemies{attack.attack.defender};
			ExchangeInitiativeQueue::appendParticipantArmies(*model, opening.get(), membership.units, ours, enemies);
			ASSERT_EQ(ours.size(), 2u);
			std::vector<uint32_t> candidateOrder;
			for(const auto * unit : ours) candidateOrder.push_back(unit->unitId());
			EXPECT_TRUE(vstd::contains(candidateOrder, ids[0]));
			EXPECT_TRUE(vstd::contains(candidateOrder, ids[1]));
			if(!specialized) ASSERT_EQ(candidateOrder.front(), ward);
			else ASSERT_NE(candidateOrder.front(), ids[1]); // First ordinary, later specialized.
			RecordProperty("PublicCandidateOrder", std::to_string(candidateOrder[0]) + "," + std::to_string(candidateOrder[1]));
			if(specialized)
			{
				// Independent native selected-AP oracle, not a guessed target ID or
				// a numeric score fitted to the failed fixture's old candidate order.
				auto before = std::make_shared<HypotheticBattle>(environment.get(), model);
				BattleExchangeVariant openingReplay;
				openingReplay.trackAttack(attack, before, cache);
				before->nextTurn(ids[2], BattleUnitTurnReason::TURN_QUEUE);
				const auto actor = before->getForUpdate(ids[2]);
				const auto target = before->getForUpdate(ids[1]);
				const auto native = AttackPossibility::evaluate(BattleAttackInfo(actor.get(), target.get(), 0, false),
					actor->getPosition(), cache, before);
				ASSERT_FALSE(native.fortuneStrikes.empty());
				EXPECT_EQ(native.fortuneStrikes.front().attackerId, ids[1]);
				EXPECT_EQ(native.fortuneStrikes.front().defenderId, ids[2]);
				BattleExchangeVariant selectedAction;
				auto applied = std::make_shared<HypotheticBattle>(environment.get(), before);
				const auto expectedAction = selectedAction.trackAttack(native, applied, cache);
				BattleExchangeVariant probes;
				const auto ordinary = probes.trackAttack(actor, before->getForUpdate(candidateOrder.front()),
					false, false, cache, before, true);
				const auto specializedProbe = probes.trackAttack(actor, target, false, false, cache, before, true);
				EXPECT_EQ(std::bit_cast<uint32_t>(specializedProbe), std::bit_cast<uint32_t>(expectedAction));
				ASSERT_GT(specializedProbe, ordinary);
				EXPECT_EQ(applied->getForUpdate(ids[1])->battlecraftPreemptiveStrikeRound, before->battleGetRound());
				EXPECT_EQ(before->getForUpdate(ids[1])->battlecraftPreemptiveStrikeRound, -1);
				RecordProperty("NativeSpecializedProbeBits", std::to_string(std::bit_cast<uint32_t>(specializedProbe)));
				RecordProperty("OrdinaryFirstProbeBits", std::to_string(std::bit_cast<uint32_t>(ordinary)));
			}
			calculator->reset();
			std::vector<JsonNode> ledger;
			std::optional<size_t> firstActorQueryEnd;
			calculator->observe = [&](const CBattleInfoCallback & actual, const DamageAttackInfo & info, const DamageEstimation & result)
			{
				if(!firstActorQueryEnd && info.attacker->unitId() == ids[2])
				{
					firstActorQueryEnd = calculator->events.size();
					const auto * child = dynamic_cast<const HypotheticBattle *>(&actual);
					ASSERT_NE(child, nullptr);
					EXPECT_TRUE(child->hasCanonicalDamageQuerySources());
					EXPECT_TRUE(heroCommands::supportedByRules(actual.getBattle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE));
					for(const auto * unit : actual.battleGetAllUnits(false))
						EXPECT_FALSE(unit->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER));
				}
				JsonNode row;
				row["actor"].Integer() = info.attacker->unitId();
				row["target"].Integer() = info.defender->unitId();
				row["minimum"].Integer() = result.damage.min;
				row["maximum"].Integer() = result.damage.max;
				row["round"].Integer() = actual.battleGetRound();
				row["protections"].Integer() = specialized ? 0 : actual.getBattle()->getHeroOrderState(
					BattleSide::ATTACKER, HeroCommand::PROTECT)->protectInterceptionsConsumed;
				for(const auto id : ids)
					row["units"].Vector().push_back(actual.battleGetUnitByID(id)->acquireState()->save());
				row["preemptive"].Integer() = actual.battleGetUnitByID(ids[1])->acquireState()->battlecraftPreemptiveStrikeRound;
				ledger.push_back(std::move(row));
			};
			BattleMovementTiming::ExchangeTiming timing;
			const auto score = evaluator.evaluateExchange(attack, 0, targets, cache, model, &timing);
			calculator->observe = {};
			ASSERT_GE(timing.targetProbes.calls, 2u);
			EXPECT_EQ(timing.selectedProbeDamageOffers, 0u);
			EXPECT_EQ(timing.selectedProbeDamageUses, 0u);
			EXPECT_EQ(timing.selectedProbeScoreUses, 0u);
			if(specialized)
			{
				if(capability == Capability::CANONICAL)
				{
					ASSERT_TRUE(firstActorQueryEnd);
					ASSERT_LT(*firstActorQueryEnd, calculator->events.size());
					EXPECT_EQ(calculator->events[*firstActorQueryEnd], 'Q'); // First ordinary bootstrap then admission.
					const auto returned = std::find_if(calculator->capabilityReturns.begin(), calculator->capabilityReturns.end(),
						[&](const auto & row) { return row.first == *firstActorQueryEnd; });
					ASSERT_NE(returned, calculator->capabilityReturns.end());
					EXPECT_EQ(returned->second, DamageForecastImplementation::CANONICAL);
				}
				const auto reaction = std::find_if(ledger.begin(), ledger.end(), [&](const auto & row)
					{ return row["actor"].Integer() == ids[1] && row["target"].Integer() == ids[2]; });
				ASSERT_NE(reaction, ledger.end());
				ASSERT_NE(std::next(reaction), ledger.end());
				EXPECT_EQ((*reaction)["preemptive"], (*reaction)["round"]);
				EXPECT_EQ(model->getForUpdate(ids[1])->battlecraftPreemptiveStrikeRound, -1);
				EXPECT_EQ((*std::next(reaction))["preemptive"], (*reaction)["round"]);
				const auto ordinary = std::find_if(ledger.begin(), reaction, [&](const auto & row)
					{ return row["actor"].Integer() == ids[2]
						&& row["target"].Integer() == candidateOrder.front() && row["preemptive"].Integer() == -1; });
				ASSERT_NE(ordinary, reaction);
				EXPECT_EQ(std::count_if(ledger.begin(), ledger.end(), [&](const auto & row)
					{ return row["actor"].Integer() == ids[1] && row["target"].Integer() == ids[2]; }), 2);
				ASSERT_NE(std::find_if(std::next(reaction), ledger.end(), [&](const auto & row)
					{ return row["preemptive"].Integer() == row["round"].Integer()
						&& row["actor"].Integer() != ids[1]; }), ledger.end());
			}
			else
			{
				const auto first = std::find_if(ledger.begin(), ledger.end(), [&](const auto & row)
					{ return row["actor"].Integer() == ids[2]; });
				ASSERT_NE(first, ledger.end());
				ASSERT_GE(ledger.end() - first, 4);
				for(int index = 0; index < 3; ++index)
				{
					EXPECT_EQ(first[index]["target"].Integer(), protector);
					EXPECT_EQ(first[index]["units"], (*first)["units"]);
				}
				EXPECT_EQ(first[3]["protections"].Integer(), 1);
			}
			if(!expected) expected = ledger; else EXPECT_EQ(ledger, *expected);
			const auto bits = std::bit_cast<uint32_t>(score);
			if(!scoreBits) scoreBits = bits; else EXPECT_EQ(bits, *scoreBits);
			const auto counts = std::pair{timing.targetProbes.calls, timing.appliedAttacks.calls};
			if(!work) work = counts; else EXPECT_EQ(counts, *work);
			const auto label = capability == Capability::UNKNOWN ? "Unknown" : "Candidate";
			JsonNode ledgerNode;
			ledgerNode.Vector() = ledger;
			RecordProperty(std::string(label) + "BoundaryLedger", ledgerNode.toCompactString());
			RecordProperty(std::string(label) + "ExchangeScoreBits", std::to_string(bits));
			RecordProperty(std::string(label) + "TargetProbes", std::to_string(timing.targetProbes.calls));
			RecordProperty(std::string(label) + "SelectedProbeOffers", std::to_string(timing.selectedProbeDamageOffers));
			RecordProperty(std::string(label) + "SelectedProbeUses", std::to_string(timing.selectedProbeDamageUses));
			EXPECT_TRUE(original.matchesSnapshot(parentBefore));
			EXPECT_EQ(gameState()->saveToMemory(), world);
			EXPECT_EQ(projectedHitTest::serialized(*battle()), native);
			EXPECT_EQ(projectedStates(), units);
			EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
			EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
			EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
		}
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

TEST_F(CreaturePressureCachePreparationTest, SelectedFirstTargetAndLaterLosingProbeKeepExactOrdinaryExchange)
{
	ASSERT_NO_FATAL_FAILURE(compareSelectedExchange(true));
}
TEST_F(CreaturePressureCachePreparationTest, TiedTargetProbesKeepExactOrdinaryExchange)
{
	ASSERT_NO_FATAL_FAILURE(compareSelectedExchange(false));
}

TEST_F(CreaturePressureCachePreparationTest, SelectedDamageLateCapabilityRevocationRetainsEveryCalculation)
{
	ASSERT_NO_FATAL_FAILURE(compareSelectedExchange(true, Capability::REVOKED_AFTER_ADMISSION));
}
TEST_F(CreaturePressureCachePreparationTest, SelectedDamageCapabilityFailureRetainsEveryCalculation)
{
	ASSERT_NO_FATAL_FAILURE(compareSelectedExchange(true, Capability::QUERY_FAILURE));
}

TEST_F(CreaturePressureCachePreparationTest, LaterWinningTargetReplacesTheEarlierProbeReceipt)
{
	ASSERT_NO_FATAL_FAILURE(compareSelectedExchange(true, Capability::CANONICAL, true));
}

TEST_F(CreaturePressureCachePreparationTest, SelectedReceiptIsSpentBeforeSecondHitAndNightProwlerAftermath)
{
	ASSERT_NO_FATAL_FAILURE(compareSelectedExchange(true, Capability::CANONICAL, false, true));
}

TEST_F(CreaturePressureCachePreparationTest, FullSelectorProtectRedirectRetainsOrdinaryLookupAndConsumption)
{
	ASSERT_NO_FATAL_FAILURE(compareSelectedFallback(false));
}
TEST_F(CreaturePressureCachePreparationTest, FullSelectorSpecializedReactionRetainsItsWholeActionLookup)
{
	ASSERT_NO_FATAL_FAILURE(compareSelectedFallback(true));
}

TEST_F(CreaturePressureCachePreparationTest, ArrowOptionalCapabilityFailurePreservesOrdinaryDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	DamageCache baseline;
	baseline.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * actor = model->battleGetUnitByID(ids[0]);
	const auto * target = model->battleGetUnitByID(ids[2]);
	const auto expected = baseline.getDamage(actor, target, model);
	const auto world = gameState()->saveToMemory(), units = projectedHitTest::serialized(*battle());
	const auto projected = projectedStates();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ScopedCalculatorRegistry registry;
	ASSERT_NE(registry.ordinary(), nullptr);
	auto calculator = std::make_shared<RecordingCalculator>(registry.ordinary(), Capability::QUERY_FAILURE);
	registry.install(calculator);
	DamageCache ordinary;
	ASSERT_NO_THROW(ordinary.buildDamageCache(model, BattleSide::ATTACKER));
	bool admitted = true;
	ASSERT_NO_THROW(admitted = arrowContinuation::scoringSupported(*model));
	EXPECT_FALSE(admitted);
	EXPECT_NE(calculator->events.find('Q'), std::string::npos);
	EXPECT_FALSE(calculator->calls.empty()); // Real ordinary calculations still succeeded.
	EXPECT_EQ(ordinary.getDamage(actor, target, model), expected);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*battle()), units);
	EXPECT_EQ(projectedStates(), projected);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
}

TEST_F(CreaturePressureCachePreparationTest, ArrowOptionalEffectCapabilityFailureRetainsOrdinaryCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast cast(model.get(), attackerSideHero, spells::Mode::HERO, arrow);
	auto mechanics = arrow->battleMechanics(&cast);
	const spells::effects::Effect * original = nullptr;
	mechanics->forEachEffect([&](const spells::effects::Effect & effect) { original = &effect; return true; });
	ASSERT_NE(original, nullptr);
	ArrowQueryFailureEffect throwing(*original);
	bool admitted = true;
	ASSERT_NO_THROW(admitted = arrowContinuation::canonicalDamageEffect(throwing, *mechanics));
	EXPECT_FALSE(admitted);
	EXPECT_EQ(throwing.queries, 1u);
	// The query decorator never replaces the real cast's implementation.
	const auto target = spells::Target{spells::Destination(model->battleGetUnitByID(ids[2]))};
	ASSERT_NO_THROW(mechanics->castEval(model->getServerCallback(), target));
}

TEST_F(CreaturePressureCachePreparationTest, ArrowUnknownBonusUpdaterRetainsOrdinaryDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(arrowContinuation::sourcesSupported(*model, ids[0]));
	DamageCache before;
	before.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * actor = model->battleGetUnitByID(ids[0]);
	const auto * target = model->battleGetUnitByID(ids[2]);
	const auto expected = before.getDamage(actor, target, model);
	Bonus harmless(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::OTHER, 0, BonusSourceID());
	harmless.updater = std::make_shared<ArrowUnknownUpdater>();
	model->getForUpdate(ids[0])->addUnitBonus({harmless});
	EXPECT_FALSE(arrowContinuation::sourcesSupported(*model, ids[0]));
	DamageCache ordinary;
	ASSERT_NO_THROW(ordinary.buildDamageCache(model, BattleSide::ATTACKER));
	EXPECT_EQ(ordinary.getDamage(actor, target, model), expected);
}
