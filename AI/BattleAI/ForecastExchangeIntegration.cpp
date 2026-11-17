/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastPerkPolicy.h"
#include "StackOwnedReadSource.h"
#include "ForecastNativePreparation.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <stdexcept>
#ifdef NH_PERF_EXPERIMENTS
#include <cstdlib>
#include <string_view>
#endif
namespace forecast
{
namespace
{
#ifdef NH_COMPACT_FORECAST
constexpr EventMask exchangePlan = eventBit(Event::Damage) | eventBit(Event::Healing)
	| eventBit(Event::Move) | eventBit(Event::Activation) | eventBit(Event::Retaliation)
	| eventBit(Event::Round) | eventBit(Event::Strike) | eventBit(Event::Effect);
#endif
void requireResult(const Result & result)
{
	if(result.refusal == Refusal::Exhausted) throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
	if(!result) throw std::logic_error("Admitted compact forecast transition rejected");
}
void require(bool accepted)
{
	if(!accepted) throw std::logic_error("Admitted compact forecast transaction rejected");
}
}
bool NativeAdapter::leadershipContextCurrent(const HypotheticBattle & branch)
{
	if(!branch.compactForecast) return false;
	if(branch.getCapturedMagicRules() != branch.compactForecast->source().magicOwner) return false;
	for(const auto id : branch.compactForecast->ids())
	{
		const auto & context = branch.compactForecast->definition(id)->leadership;
		if(!context.known) return false;
		const auto side = context.controllingSide;
		if(side == BattleSide::NONE) { if(context.hero) return false; else continue; }
		if(branch.getLeadershipComposition(side) != context.composition) return false;
		const auto policy = branch.battleGetVisibleHeroPolicy(side);
		if(!context.hero)
		{
			if(policy.kind() != VisibleHeroPolicy::Knowledge::KNOWN_ABSENT) return false;
			continue;
		}
		if(policy.kind() != VisibleHeroPolicy::Knowledge::VISIBLE_HERO) return false;
		const auto * hero = policy.legacyHero();
		if(!hero || hero->id != *context.hero || hero->level != context.heroLevel
			|| hero->getTreeVersion() != context.heroTreeVersion
			|| hero->getCapturedCapabilityRules() != context.capabilityOwner
			|| hero->getPerkState().capturedOwner() != context.perkOwner) return false;
	}
	return true;
}
bool NativeAdapter::prepareExchange(HypotheticBattle & branch, Counters * preparation)
{
#ifndef NH_COMPACT_FORECAST
	(void)branch;
	(void)preparation;
	return false;
#else
	if(branch.referenceForecastOnly()) return false;
	if(branch.compactForecast && leadershipContextCurrent(branch)) return true;
	if(branch.compactForecast)
	{
		// Only BEFORE a new continuation: changed policy must be recaptured or
		// reference-admitted, never silently retained across a Hero/profile change.
		branch.compactForecast.reset(); branch.compactAdmissionAttempted = false;
	}
	if(branch.compactAdmissionAttempted) return false;
	if(preparation) preparation->add(&Counters::rootCaptures);
#ifdef NH_PERF_EXPERIMENTS
	// Measure the reference route in the same experimental binary.
	const auto * forceReference = std::getenv("NH_PERF_FORCE_REFERENCE");
	if(forceReference && std::string_view(forceReference) == "1")
	{
		branch.compactAdmissionAttempted = true;
		if(preparation) preparation->add(&Counters::referenceOverrides);
		return false;
	}
#endif
	// This optional capture is outside the mutated continuation. Refusal keeps
	// the original reference path, including all existing reactions.
	const auto roster = branch.battleGetAllUnits(false);
	const bool livingRoot = std::ranges::all_of(roster, [](const battle::Unit * unit) { return unit->alive(); });
	const auto plan = livingRoot ? exchangePlan : exchangePlan & ~eventBit(Event::Healing);
	NativePreparation rootPreparation(branch);
	auto admitted = capture(branch, plan);
	branch.compactAdmissionAttempted = true;
	if(!admitted.state)
	{
		if(preparation) preparation->add(admitted.failure == AdmissionFailure::UnitLifecycle
			|| admitted.failure == AdmissionFailure::DynamicHealth ? &Counters::lifecycleFallbacks : &Counters::sourceFallbacks);
		return false;
	}
	std::vector<std::pair<std::shared_ptr<StackWithBonuses>, std::unique_ptr<PreparedOwnedReadSource>>> sealed;
	sealed.reserve(admitted.state->ids().size());
	try
	{
	for(const auto id : admitted.state->orderedIds())
	{
		auto target = branch.stackStates.at(id);
		const auto * value = admitted.state->unit(id);
		sealed.emplace_back(target, target->prepareOwnedReadSource(admitted.state->readView(id), value->currentModifiers, value->effects));
	}
	}
	catch(const battleSearch::BudgetExhausted &) { throw; }
	catch(const std::exception &)
	{
		if(preparation) preparation->add(&Counters::sourceFallbacks);
		return false;
	}
	rootPreparation.accept({});
	for(auto & [target, source] : sealed) target->publishOwnedReadSource(*source);
	if(preparation) preparation->add(&Counters::rootAdmissions);
	branch.compactForecast = std::move(admitted.state);
	return true;
#endif
}
std::shared_ptr<HypotheticBattle> NativeAdapter::prepareExchangeBranch(const Environment * environment,
	const std::shared_ptr<HypotheticBattle> & source, Counters * counters)
{
	// Capture after the caller's existing source/participant decisions. This
	// optional admission never changes the legal candidate universe.
	prepareExchange(*source, counters);
	auto child = std::make_shared<HypotheticBattle>(environment, source,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, source->searchBudget());
	forkExchange(*source, *child);
	return child;
}
State NativeAdapter::forkOwnedState(const HypotheticBattle & source)
{
	const auto * values = source.compactForecast ? &*source.compactForecast
		: source.compactReadSession ? &*source.compactReadSession : nullptr;
	require(values != nullptr);
	std::vector<std::optional<int32_t>> caches;
	caches.reserve(values->orderedIds().size());
	for(const auto id : values->orderedIds())
	{
		const auto found = source.stackStates.find(id);
		if(found == source.stackStates.end()) caches.emplace_back();
		else
		{
			require(found->second && found->second->unitId() == id && found->second->owner == &source);
			caches.push_back(battle::UnitStateValueSnapshot::retaliationCache(*found->second));
		}
	}
	return values->fork(caches);
}
void NativeAdapter::forkExchange(const HypotheticBattle & source, HypotheticBattle & child)
{
	require(!child.compactForecast.has_value());
	if(source.compactForecast)
	{
		if(child.compactReadSession && child.compactReadSession->sameSnapshot(*source.compactForecast))
		{
			// Constructor already isolated the cheap native reader/cache context.
			child.compactForecast = std::move(child.compactReadSession);
			child.compactReadSession.reset();
		}
		else throw std::invalid_argument("Compact fork requires its exact constructor source capability");
	}
}
bool NativeAdapter::afterAttack(HypotheticBattle & branch, battle::CUnitState & target,
	bool ranged, bool counter, bool physical, bool accepted)
{
	if(afterFocusAttack(branch, target, ranged, counter, physical, accepted)) return true;
	if(!branch.compactForecast) return false;
	require(branch.compactForecast->supports(Event::Strike));
	auto * isolated = dynamic_cast<StackWithBonuses *>(&target);
	require(isolated && branch.battleGetUnitByID(target.unitId()) == isolated
		&& target.usesStateEnvironment(*branch.unitEnvironment()));
	auto draft = *branch.compactForecast;
	const auto id = target.unitId();
	const auto receipt = prepareEffectApplication(draft, branch, id, {}, {});
	auto staged = prepareEffectDraft(*isolated);
	// Use the original native bookkeeping ONCE, entirely on an owning draft.
	// Native CAmmo/cache queries cannot leave a half-mutated original receiver.
	staged->afterAttack(ranged, counter, physical);
	require(stageEffects(draft, branch, id, receipt, *staged));
	refreshReadViews(draft, branch, std::span(&id, 1), false);
	publishEffects(branch, std::move(draft), *isolated, *staged);
	return true;
}

void NativeAdapter::completeActivation(HypotheticBattle & branch, UnitId id)
{
	if(!branch.compactForecast) return;
	auto draft = *branch.compactForecast;
	requireResult(Transitions::consumeActivation(draft, id, branch.searchBudget().get()));
	branch.compactForecast = std::move(draft);
}
void NativeAdapter::reserveReceipt(HypotheticBattle & branch)
{
	if(branch.compactForecast && branch.searchBudget())
	{
		// Movement may change a current Order's native beneficiary. Reserve its
		// entire bounded roster refresh before any accepted receipt prefix.
		const bool orders = !branch.localHeroOrderStates(BattleSide::ATTACKER).empty()
			|| !branch.localHeroOrderStates(BattleSide::DEFENDER).empty();
		branch.searchBudget()->require(battleSearch::WorkKind::Node,
			1 + (orders ? static_cast<uint32_t>(branch.compactForecast->ids().size()) : 0));
	}
}
void NativeAdapter::importReceipt(HypotheticBattle & branch, const battle::CUnitState & target)
{
	if(!branch.compactForecast) return;
	require(branch.battleGetUnitByID(target.unitId()) == &target
		&& target.usesStateEnvironment(*branch.unitEnvironment()));
	auto draft = *branch.compactForecast;
	importFocusReceipt(draft, target, branch);
	const auto * current = draft.unit(target.unitId());
	require(current && current->health.available() == target.health.available()
		&& current->health.getCount() == target.health.getCount());
	auto next = *current;
	const bool moved = next.position != target.getPosition();
	// SelectedAttackReceipt already validated native movement and copied exact
	// native resource provenance. Import only that receipt, never source stats.
	next.position = target.getPosition();
	next.activation.shots = {target.shots.available(), target.shots.total(), target.shots.isLimited()};
	const auto retal = target.counterAttacks.observeWithoutLatch();
	next.activation.retaliation = {retal.available, retal.total, retal.limited};
	next.activation.moved = target.movedThisRound;
	next.activation.waited = target.waitedThisTurn;
	next.activation.waiting = target.waiting;
	next.activation.defending = target.defending;
	next.activation.hadMorale = target.hadMorale;
	next.activation.waitBonusUsed = target.battlecraftWaitBonusUsed;
	next.activation.waitMasteryDoubled = target.battlecraftWaitMasteryDoubled;
	next.veteranPhysicalDamage = target.veteranPhysicalDamageSinceActivation;
	draft.commit(target.unitId(), std::move(next), Queue | (moved ? Occupancy | Adjacency | Damage : Clean), moved);
	if(moved) refreshOrderDerived(branch, draft); // Work was admitted by reserveReceipt before the native prefix.
	else
	{
		const auto * staged = dynamic_cast<const StackWithBonuses *>(&target);
		require(staged != nullptr);
		stageNativeReadValue(draft, target.unitId(), target, staged->preparedModifierWarfare());
		const auto id = target.unitId();
		refreshReadViews(draft, branch, std::span(&id, 1), false);
	}
	branch.compactForecast = std::move(draft);
}
WorkScope::~WorkScope()
{
	if(sink && branch.forecastState()) sink->accumulate(branch.forecastState()->work());
}
}
