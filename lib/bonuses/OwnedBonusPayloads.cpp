/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedBonusPayloads.h"
#include "BonusParameters.h"
#include "Limiters.h"
#include "Propagators.h"
#include "Updaters.h"
#include "../serializer/CMemorySerializer.h"
#include <stdexcept>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
namespace
{
struct Inventory
{
	std::vector<std::shared_ptr<const Bonus>> bonuses;
	std::vector<TUpdaterPtr> updaters;
	std::vector<TLimiterPtr> limiters;
	std::unordered_map<const Bonus *, size_t> bonusIndex;
	std::unordered_map<const IUpdater *, size_t> updaterIndex;
	std::unordered_map<const ILimiter *, size_t> limiterIndex;
	void add(const std::shared_ptr<const Bonus> & value)
	{
		if(value && bonusIndex.emplace(value.get(), bonuses.size()).second) bonuses.push_back(value);
	}
	void add(const TUpdaterPtr & value)
	{
		if(value && updaterIndex.emplace(value.get(), updaters.size()).second) updaters.push_back(value);
	}
	void add(const TLimiterPtr & value)
	{
		if(value && limiterIndex.emplace(value.get(), limiters.size()).second) limiters.push_back(value);
	}
	void inspectBonuses()
	{
		for(size_t i = 0; i < bonuses.size(); ++i)
		{
			const auto value = bonuses[i]; // Adding nested values may reallocate the vector.
			if(typeid(*value) != typeid(Bonus)) throw std::invalid_argument("Unknown owned bonus record type");
			add(value->updater);
			add(value->propagationUpdater);
			add(value->limiter);
			if(value->propagator && typeid(*value->propagator) != typeid(IPropagator)
				&& typeid(*value->propagator) != typeid(CPropagatorNodeType))
				throw std::invalid_argument("Unknown owned bonus propagator type");
			if(const auto * parameters = value->parameters ? value->parameters->combatEffects() : nullptr)
				for(const auto & effect : parameters->effects)
					if(const auto * child = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect))
						add(child->bonus);
		}
	}
	void inspectUpdaters()
	{
		for(size_t i = 0; i < updaters.size(); ++i)
		{
			const auto value = updaters[i];
			const auto & type = typeid(*value);
			if(type == typeid(CompositeUpdater))
			{
				for(const auto & child : static_cast<const CompositeUpdater &>(*value).updaters) add(child);
				continue;
			}
			if(type != typeid(IUpdater) && type != typeid(GrowsWithLevelUpdater)
				&& type != typeid(TimesHeroLevelUpdater) && type != typeid(TimesArmySizeUpdater)
				&& type != typeid(TimesStackSizeUpdater) && type != typeid(TimesStackLevelUpdater)
				&& type != typeid(DivideStackLevelUpdater) && type != typeid(TimesHeroLevelDivideStackLevelUpdater)
				&& type != typeid(OwnerUpdater))
				throw std::invalid_argument("Unknown owned bonus updater type");
		}
	}
	void inspectLimiters()
	{
		for(size_t i = 0; i < limiters.size(); ++i)
		{
			const auto value = limiters[i];
			const auto & type = typeid(*value);
			if(type == typeid(AllOfLimiter) || type == typeid(AnyOfLimiter) || type == typeid(NoneOfLimiter))
			{
				for(const auto & child : static_cast<const AggregateLimiter &>(*value).limiters) add(child);
				continue;
			}
			if(type != typeid(ILimiter) && type != typeid(CCreatureTypeLimiter)
				&& type != typeid(HasAnotherBonusLimiter) && type != typeid(TerrainLimiter)
				&& type != typeid(CreatureLevelLimiter) && type != typeid(FactionLimiter)
				&& type != typeid(CreatureAlignmentLimiter) && type != typeid(OppositeSideLimiter)
				&& type != typeid(RankRangeLimiter) && type != typeid(UnitOnHexLimiter)
				&& type != typeid(UnitAdjacentLimiter) && type != typeid(HasChargesLimiter))
				throw std::invalid_argument("Unknown owned bonus limiter type");
		}
	}
};
void requireAcyclic(const std::vector<std::vector<size_t>> & edges)
{
	std::vector<uint8_t> color(edges.size());
	for(size_t root = 0; root < edges.size(); ++root)
	{
		if(color[root]) continue;
		std::vector<std::pair<size_t, size_t>> path{{root, 0}};
		color[root] = 1;
		while(!path.empty())
		{
			auto & top = path.back();
			if(top.second == edges[top.first].size())
			{
				color[top.first] = 2;
				path.pop_back();
				continue;
			}
			const auto child = edges[top.first][top.second++];
			if(color[child] == 1) throw std::invalid_argument("Cyclic owned bonus modifier graph");
			if(!color[child]) { color[child] = 1; path.emplace_back(child, 0); }
		}
	}
}
void requireModifierDAG(const Inventory & source)
{
	std::vector<std::vector<size_t>> edges(source.updaters.size());
	for(size_t i = 0; i < source.updaters.size(); ++i)
		if(const auto * composite = dynamic_cast<const CompositeUpdater *>(source.updaters[i].get()))
			for(const auto & child : composite->updaters)
				if(child) edges[i].push_back(source.updaterIndex.at(child.get()));
	requireAcyclic(edges);
	edges.assign(source.limiters.size(), {});
	for(size_t i = 0; i < source.limiters.size(); ++i)
		if(const auto * aggregate = dynamic_cast<const AggregateLimiter *>(source.limiters[i].get()))
			for(const auto & child : aggregate->limiters)
				if(child) edges[i].push_back(source.limiterIndex.at(child.get()));
	requireAcyclic(edges);
}
Inventory inspect(const std::vector<std::shared_ptr<const Bonus>> & roots)
{
	Inventory source;
	for(const auto & root : roots) source.add(root);
	source.inspectBonuses();
	source.inspectUpdaters();
	source.inspectLimiters();
	requireModifierDAG(source); // Before AggregateLimiter codec preflights can recurse.
	return source;
}
template<typename T>
void requireSame(const std::vector<std::shared_ptr<const T>> & source, const std::vector<std::shared_ptr<T>> & clone)
{
	if(source.size() != clone.size()) throw std::logic_error("Owned payload clone cardinality mismatch");
	std::unordered_set<const T *> originals;
	std::unordered_set<const T *> copies;
	for(const auto & value : source) originals.insert(value.get());
	for(size_t i = 0; i < source.size(); ++i)
		if(!source[i] || !clone[i] || typeid(*source[i]) != typeid(*clone[i])
			|| originals.contains(clone[i].get()) || !copies.insert(clone[i].get()).second)
			throw std::logic_error("Owned payload clone identity mismatch");
}
}
void ownedBonusPayloads::validateTypesAndTopology(const std::vector<std::shared_ptr<const Bonus>> & roots)
{
	(void)inspect(roots);
}
std::unique_ptr<ownedBonusPayloads::Captured> ownedBonusPayloads::copy(
	const std::vector<std::shared_ptr<const Bonus>> & roots)
{
	return copy(roots, OwnedBonusCaptureCodec::current());
}
std::unique_ptr<ownedBonusPayloads::Captured> ownedBonusPayloads::copy(
	const std::vector<std::shared_ptr<const Bonus>> & roots, const OwnedBonusCaptureCodec & codec)
{
	const auto source = inspect(roots);
	CMemorySerializer memory; // One identity map for every raw list and nested payload; no callback.
	memory.oser.version = codec.version();
	memory.iser.version = codec.version();
	memory.oser & source.bonuses;
	memory.oser & source.updaters;
	memory.oser & source.limiters;
	auto result = std::unique_ptr<Captured>(new Captured);
	auto & bonuses = result->bonuses;
	auto & updaters = result->updaters;
	auto & limiters = result->limiters;
	memory.iser & bonuses;
	memory.iser & updaters;
	memory.iser & limiters;
	requireSame(source.bonuses, bonuses);
	requireSame(source.updaters, updaters);
	requireSame(source.limiters, limiters);
	for(size_t i = 0; i < bonuses.size(); ++i) bonuses[i]->bonusOwner = source.bonuses[i]->bonusOwner;
	for(size_t i = 0; i < updaters.size(); ++i)
		if(typeid(*source.updaters[i]) == typeid(TimesStackSizeUpdater))
			static_cast<TimesStackSizeUpdater &>(*updaters[i]) = static_cast<const TimesStackSizeUpdater &>(*source.updaters[i]);
	for(size_t i = 0; i < limiters.size(); ++i)
		if(typeid(*source.limiters[i]) == typeid(UnitOnHexLimiter))
			static_cast<UnitOnHexLimiter &>(*limiters[i]) = static_cast<const UnitOnHexLimiter &>(*source.limiters[i]);
	result->rootValues.reserve(roots.size());
	for(const auto & root : roots) result->rootValues.push_back(root ? bonuses.at(source.bonusIndex.at(root.get())) : nullptr);
	return result;
}

ownedBonusPayloads::Captured::~Captured()
{
	// All recipe/query borrows have retired; these are exclusively copied objects.
	for(const auto & value : bonuses) if(value) value->parameters.reset();
	for(const auto & value : updaters)
		if(auto * composite = dynamic_cast<CompositeUpdater *>(value.get())) composite->updaters.clear();
	for(const auto & value : limiters)
		if(auto * aggregate = dynamic_cast<AggregateLimiter *>(value.get())) aggregate->limiters.clear();
}
