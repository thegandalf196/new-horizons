/*
 * ProjectedBattleWorldArena.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "BattleInfo.h"
#include "UnitStateValueFields.h"
#include "UnitAdditionRules.h"
#include "../spells/NewHorizonsElementalTerrain.h"
#include <stdexcept>

battle::ProjectedBattleWorldArena::Data::Data(BattleInfo & source)
	: source(source), roster({}, ProjectedBonusRoster::Identity::OrderedOccurrences),
	sideValues(ProjectedUnitEnvironmentState::capture(source)),
	placementValues(BattleAccessibilityState::capture(source)), commandRules(source.getHeroCommandRules()),
	round(source.battleGetRound()), graph(std::make_unique<ProjectedBonusGraph>())
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		orders.at(side) = source.getHeroOrderStates(side);
		reduced.at(side) = source.getReducedExtraActivationState(side);
		allowances.at(side) = source.getHeroActionAllowances(side);
		chainUsed.at(side) = source.getRebirthChainUsed(side);
		sparkUsed.at(side) = source.getPhoenixSparkUsed(side);
		armies.at(side) = source.battleGetArmyObject(side);
		queryArmies.at(side) = source.getSideArmy(side);
		heroes.at(side) = source.battleGetFightingHero(side);
		sideHeroes.at(side) = source.getSideHero(side);
	}
	battleId = source.getBattleID();
	primaryElemental = newHorizonsElementalTerrain::primaryElemental(source);
	crisis = source.getCrisisCommandState();
	seize = source.getSeizeInitiativeState();
	destroyedUnits = source.bloodrageDestroyedUnits;
	firstBlood = source.bloodrageFirstBloodUsed;
	categories = source.getCreatureCategoryRules();
	sidecarOrigin = std::make_unique<const BattleUnitSidecarOrigin>(BattleUnitSidecarOrigin{
		copyBattleSidecarTuple(BattleUnitSidecarFields::writable(std::as_const(source))),
		copyBattleSidecarTuple(BattleUnitSidecarFields::inputs(std::as_const(source))), primaryElemental});
	rootOrigin = PreparedBattleUnitRoots::captureOrigin(source);
	captureUnits();
	environment = std::make_unique<ProjectedUnitEnvironment>(roster, *graph, sideValues);
	for(auto & slot : slots)
		slot->state->bindStateEnvironment(environment.get());
	placement = std::make_unique<ProjectedBattleAccessibilityContext>(placementValues, roster, *graph);
}

void battle::ProjectedBattleWorldArena::Data::captureUnits()
{
	std::vector<const Unit *> units;
	for(const auto & live : source.stacks)
	{
		auto * original = live.get();
		if(!original)
			throw std::invalid_argument("Projected arena requires nonnull original stack occurrences");
		auto slot = std::make_unique<Slot>();
		slot->original = original;
		slots.push_back(std::move(slot)); // retain descriptor BEFORE any graph capture
		auto & held = *slots.back();
		held.state = std::make_unique<CUnitStateDetached>(original, &graph->view(*original));
		UnitStateValueFields::copy(*held.state, *original); // no ordinary-provider/form hook
		units.push_back(held.state.get());
	}
	// Only assignment occurs BEFORE any binding/adapter borrows this roster.
	roster = ProjectedBonusRoster(std::move(units), ProjectedBonusRoster::Identity::OrderedOccurrences);
	for(const auto & slot : slots)
		graph->bindOccurrence(*slot->original, *slot->state, roster);
}

void battle::ProjectedBattleWorldArena::Data::requireUsable() const
{
	if(failed || finalizer || rootStorageTransferred)
		throw std::logic_error("Projected world arena failed or was frozen; discard or retire its owner");
}

void battle::ProjectedBattleWorldArena::Data::abandon() noexcept
{
	failed = true;
	graph->abandonPreparation();
}

battle::ProjectedBattleWorldArena::Data::Slot * battle::ProjectedBattleWorldArena::Data::find(uint32_t id, bool onlyAlive) const
{
	const auto found = std::find_if(slots.begin(), slots.end(), [id, onlyAlive](const auto & slot)
	{
		return slot->state->unitId() == id && (!onlyAlive || slot->state->alive());
	});
	return found == slots.end() ? nullptr : found->get();
}

battle::ProjectedBattleWorldArena::ProjectedBattleWorldArena(BattleInfo & source) : impl(std::make_unique<Data>(source)) {}
battle::ProjectedBattleWorldArena::~ProjectedBattleWorldArena() = default;

size_t battle::ProjectedBattleWorldArena::size() const { impl->requireUsable(); return impl->slots.size(); }
uint32_t battle::ProjectedBattleWorldArena::nextUnitId() const
{
	impl->requireUsable();
	return nextUnitAdditionId(impl->slots.size(), impl->commandRules);
}
const battle::CUnitStateDetached & battle::ProjectedBattleWorldArena::unitAt(size_t index) const
{
	impl->requireUsable();
	return *impl->slots.at(index)->state;
}
const CStack & battle::ProjectedBattleWorldArena::originalAt(size_t index) const
{
	impl->requireUsable();
	return *impl->slots.at(index)->original;
}
JsonNode battle::ProjectedBattleWorldArena::snapshotAt(size_t index)
{
	impl->requireUsable();
	try { return impl->slots.at(index)->state->save(); }
	catch(...) { impl->abandon(); throw; }
}
const battle::CUnitStateDetached * battle::ProjectedBattleWorldArena::findUnit(uint32_t id, bool alive) const
{
	impl->requireUsable();
	const auto * found = impl->find(id, alive);
	return found ? found->state.get() : nullptr;
}
const ProjectedBonusRoster & battle::ProjectedBattleWorldArena::roster() const { impl->requireUsable(); return impl->roster; }
const battle::IUnitEnvironment & battle::ProjectedBattleWorldArena::environment() const { impl->requireUsable(); return *impl->environment; }
AccessibilityInfo battle::ProjectedBattleWorldArena::accessibility() const
{
	impl->requireUsable();
	return buildBattleAccessibility(*impl->placement);
}
size_t battle::ProjectedBattleWorldArena::retainedQueryCount() const { impl->requireUsable(); return impl->queries.size(); }
