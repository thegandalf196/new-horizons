/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastState.h"
#include "ForecastNativeReadView.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace forecast
{
struct State::Root
{
	SourceIdentity source;
	std::vector<UnitId> ids;
	std::vector<UnitId> order;
	std::vector<size_t> slots;
	std::vector<UnitInput> units;
	EventMask planned = 0;
};
struct State::Layer
{
	std::vector<std::shared_ptr<const UnitDelta>> units;
	std::vector<uint32_t> dirty;
	uint64_t revision = 0;
	explicit Layer(size_t size) : units(size), dirty(size, Clean) {}
};
State::State(std::shared_ptr<const Root> root, std::shared_ptr<const Layer> layer, Counters counters)
	: root(std::move(root)), layer(std::move(layer)), counters(counters), round(this->root->source.round) {}
State State::capture(SourceIdentity source, std::vector<UnitInput> units, EventMask planned)
{
	if(units.empty() || units.size() > 4096 || planned >> static_cast<unsigned>(Event::Count))
		throw std::invalid_argument("Invalid compact forecast root shape");
	auto root = std::make_shared<Root>();
	root->source = source;
	root->planned = planned;
	std::vector<std::pair<UnitId, size_t>> index;
	for(size_t slot = 0; slot < units.size(); ++slot)
	{
		auto & unit = units[slot];
		if(!unit.definition || !unit.state.health.isDetached())
			throw std::invalid_argument("Forecast root must own definitions and detached health");
		const auto & definition = *unit.definition;
		const auto context = unit.state.health.valueContext();
		if(definition.leadership.known && definition.leadership.active
			&& (definition.leadership.capacity < 1 || definition.leadership.requirement < 1 || definition.leadership.effectiveLeadership < 0))
			throw std::invalid_argument("Invalid captured native Leadership context");
		if(definition.baseAmount != context.baseAmount || definition.maximumHealth != context.maximumHealth
			|| (definition.supported & planned) != planned || !unit.state.position.isValid()
			|| unit.state.activation.shots.available < 0 || unit.state.activation.retaliation.available < 0
			|| unit.state.activation.casts.available < 0 || unit.state.veteranPhysicalDamage < 0)
			throw std::invalid_argument("Forecast root identity, context or capability mismatch");
		index.emplace_back(definition.id, slot);
		root->order.push_back(definition.id);
		auto ownedDefinition = std::make_shared<UnitDefinition>(definition);
		for(size_t mode = 0; mode < 2; ++mode)
		{
			const auto original = ownedDefinition->numeric[mode];
			if(original) ownedDefinition->numeric[mode] = std::make_shared<const nativeDamage::PreparedUnit>(*original);
			if(unit.state.numeric[mode])
			{
				const auto & facts = unit.state.numeric[mode]->unit;
				if(facts.count != unit.state.health.getCount() || facts.firstHealth != unit.state.health.getFirstHPleft()
					|| facts.maxHealth != context.maximumHealth)
					throw std::invalid_argument("Forecast derived health inputs do not match native ledger");
				unit.state.numeric[mode] = unit.state.numeric[mode] == original
					? ownedDefinition->numeric[mode]
					: std::make_shared<const nativeDamage::PreparedUnit>(*unit.state.numeric[mode]);
			}
		}
		unit.definition = std::move(ownedDefinition);
	}
	std::sort(index.begin(), index.end());
	for(const auto & [id, slot] : index)
	{
		if(!root->ids.empty() && root->ids.back() == id)
			throw std::invalid_argument("Duplicate forecast UnitId");
		root->ids.push_back(id);
		root->slots.push_back(slot);
	}
	root->units = std::move(units);
	auto overlay = std::make_shared<Layer>(root->units.size());
	return State(std::move(root), std::move(overlay), {});
}
State State::fork(std::span<const std::optional<int32_t>> nativeCaches) const
{
	if(!nativeCaches.empty() && nativeCaches.size() != root->units.size())
		throw std::invalid_argument("Wrong native observation cache roster at fork");
	Counters childCounters; // Child diagnostics are DELTAS, never inherited work.
	childCounters.add(&Counters::forks);
	auto child = State(root, layer, childCounters);
	child.round = round;
	std::vector<std::shared_ptr<const NativeReadView>> current;
	current.reserve(root->units.size());
	bool any = false, missing = false;
	for(const auto & input : root->units)
	{
		auto view = readView(input.definition->id);
		any |= view != nullptr; missing |= view == nullptr;
		current.push_back(std::move(view));
	}
	if(any)
	{
		if(missing) throw std::logic_error("Incomplete native reader ownership at fork");
		child.readerViews = std::make_shared<const std::vector<std::shared_ptr<const NativeReadView>>>(
			NativeReadView::forkViews(current, nativeCaches));
	}
	return child;
}
const UnitDefinition * State::definition(UnitId id) const
{
	const auto found = std::lower_bound(root->ids.begin(), root->ids.end(), id);
	return found == root->ids.end() || *found != id ? nullptr
		: root->units[root->slots[static_cast<size_t>(found - root->ids.begin())]].definition.get();
}
const UnitDelta * State::unit(UnitId id) const
{
	const auto found = std::lower_bound(root->ids.begin(), root->ids.end(), id);
	if(found == root->ids.end() || *found != id) return nullptr;
	const auto slot = root->slots[static_cast<size_t>(found - root->ids.begin())];
	return layer->units[slot] ? layer->units[slot].get() : &root->units[slot].state;
}
std::shared_ptr<const NativeReadView> State::readView(UnitId id) const
{
	const auto found = std::lower_bound(root->ids.begin(), root->ids.end(), id);
	if(found == root->ids.end() || *found != id) return {};
	const auto slot = root->slots[static_cast<size_t>(found - root->ids.begin())];
	if(readerViews) return (*readerViews)[slot];
	const auto * value = unit(id);
	return value ? value->readView : nullptr;
}
const nativeDamage::PreparedUnit * State::prepared(UnitId id, bool shooting) const
{
	const auto * value = unit(id);
	const auto * result = value ? value->numeric[shooting ? 1 : 0].get() : nullptr;
	counters.add(result ? &Counters::compactFactHits : &Counters::compactFactMisses);
	return result;
}
std::span<const UnitId> State::ids() const { return root->ids; }
const SourceIdentity & State::source() const { return root->source; }
bool State::supports(Event event) const { return (root->planned & eventBit(event)) != 0; }
uint64_t State::revision() const { return layer ? layer->revision : 0; }
uint32_t State::dirty(UnitId id) const
{
	const auto found = std::lower_bound(root->ids.begin(), root->ids.end(), id);
	return found == root->ids.end() || *found != id ? Clean
		: layer->dirty[root->slots[static_cast<size_t>(found - root->ids.begin())]];
}
void State::commit(UnitId id, UnitDelta state, uint32_t dirty, bool neighborhood)
{
	if(!definition(id) || !state.health.isDetached())
		throw std::invalid_argument("Forecast commit has unknown identity or borrowed health");
	if(revision() == std::numeric_limits<uint64_t>::max())
		throw std::overflow_error("Forecast transition revision overflow");
	const auto found = std::lower_bound(root->ids.begin(), root->ids.end(), id);
	const auto slot = root->slots[static_cast<size_t>(found - root->ids.begin())];
	const auto currentView = readView(id);
	if(state.readView == unit(id)->readView) state.readView = currentView;
	auto readers = readerViews;
	if(readers && (*readers)[slot] != state.readView)
	{
		auto replacement = std::make_shared<std::vector<std::shared_ptr<const NativeReadView>>>(*readers);
		(*replacement)[slot] = state.readView;
		readers = std::move(replacement);
	}
	auto next = std::make_shared<Layer>(*layer);
	next->units[slot] = std::make_shared<const UnitDelta>(std::move(state));
	next->dirty[slot] |= dirty;
	if(neighborhood) for(auto & flags : next->dirty) flags |= Adjacency | Damage | Occupancy;
	next->revision = revision() + 1;
	layer = std::move(next); // Allocation/copy completed before the branch changes.
	readerViews = std::move(readers);
	counters.add(&Counters::committedTransitions);
	if(neighborhood) counters.add(&Counters::neighborhoodInvalidations);
}
}

std::span<const forecast::UnitId> forecast::State::orderedIds() const noexcept { return root->order; }
