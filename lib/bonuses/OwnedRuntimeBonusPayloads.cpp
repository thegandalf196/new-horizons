/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedBonusPayloads.h"
#include "BonusParameters.h"
#include "Limiters.h"
#include "Propagators.h"
#include "Updaters.h"
#include <stdexcept>
#include <typeinfo>
#include <unordered_map>
namespace
{
class Clone;
/// Only the copied updater's private divider edge is visited. Scalar fields are
/// already faithfully copied, including fields omitted by the binary format.
struct DividerEdge
{
	Clone & clone;
	template<class T> void operator&(T &) const {}
	void operator&(std::shared_ptr<DivideStackLevelUpdater> & value) const;
};
class Clone
{
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> bonusAliases;
	std::unordered_map<const IUpdater *, std::shared_ptr<IUpdater>> updaterAliases;
	std::unordered_map<const ILimiter *, std::shared_ptr<ILimiter>> limiterAliases;
	std::unordered_map<const IPropagator *, std::shared_ptr<IPropagator>> propagatorAliases;
	std::unordered_map<const BonusParameters *, std::shared_ptr<BonusParameters>> parameterAliases;
	template<class T, class Base> static std::shared_ptr<Base> exact(const std::shared_ptr<const Base> & value)
	{
		return std::make_shared<T>(static_cast<const T &>(*value));
	}
	std::shared_ptr<BonusParameters> parameters(const TBonusParametersPtr & source)
	{
		if(!source) return {};
		auto [entry, inserted] = parameterAliases.try_emplace(source.get());
		if(!inserted) return entry->second;
		auto value = std::make_shared<BonusParameters>(*source);
		entry->second = value; // Install identity BEFORE following possible Bonus cycles.
		if(value->combatEffects())
			for(auto & effect : value->toCustom<BonusParametersOnCombatEvent>().effects)
				if(auto * owned = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect))
					owned->bonus = bonus(owned->bonus);
		return value;
	}
	std::shared_ptr<IPropagator> propagator(const TPropagatorPtr & source)
	{
		if(!source) return {};
		auto [entry, inserted] = propagatorAliases.try_emplace(source.get());
		if(!inserted) return entry->second;
		if(typeid(*source) == typeid(IPropagator)) entry->second = exact<IPropagator>(source);
		else if(typeid(*source) == typeid(CPropagatorNodeType)) entry->second = exact<CPropagatorNodeType>(source);
		else throw std::invalid_argument("Unknown owned bonus propagator type");
		return entry->second;
	}
public:
	std::vector<std::shared_ptr<Bonus>> bonuses;
	std::vector<std::shared_ptr<IUpdater>> updaters;
	std::vector<std::shared_ptr<ILimiter>> limiters;
	~Clone()
	{
		// Exception safety: only copied objects are broken. Successful transfer
		// empties these vectors; Captured then owns the identical retirement job.
		for(const auto & value : bonuses) value->parameters.reset();
		for(const auto & value : updaters)
			if(auto * composite = dynamic_cast<CompositeUpdater *>(value.get())) composite->updaters.clear();
		for(const auto & value : limiters)
			if(auto * group = dynamic_cast<AggregateLimiter *>(value.get())) group->limiters.clear();
	}
	std::shared_ptr<Bonus> bonus(const std::shared_ptr<const Bonus> & source)
	{
		if(!source) return {};
		auto [entry, inserted] = bonusAliases.try_emplace(source.get());
		if(!inserted) return entry->second;
		auto value = std::make_shared<Bonus>(*source); entry->second = value; bonuses.push_back(value);
		value->parameters = parameters(source->parameters);
		value->propagator = propagator(source->propagator);
		value->updater = updater(source->updater);
		value->propagationUpdater = updater(source->propagationUpdater);
		value->limiter = limiter(source->limiter);
		return value;
	}
	std::shared_ptr<IUpdater> updater(const TUpdaterPtr & source)
	{
		if(!source) return {};
		auto [entry, inserted] = updaterAliases.try_emplace(source.get());
		if(!inserted) return entry->second;
		std::shared_ptr<IUpdater> value;
		const auto & type = typeid(*source);
		if(type == typeid(IUpdater)) value = exact<IUpdater>(source);
		else if(type == typeid(GrowsWithLevelUpdater)) value = exact<GrowsWithLevelUpdater>(source);
		else if(type == typeid(TimesHeroLevelUpdater)) value = exact<TimesHeroLevelUpdater>(source);
		else if(type == typeid(TimesArmySizeUpdater)) value = exact<TimesArmySizeUpdater>(source);
		else if(type == typeid(TimesStackSizeUpdater)) value = exact<TimesStackSizeUpdater>(source);
		else if(type == typeid(TimesStackLevelUpdater)) value = exact<TimesStackLevelUpdater>(source);
		else if(type == typeid(DivideStackLevelUpdater)) value = exact<DivideStackLevelUpdater>(source);
		else if(type == typeid(TimesHeroLevelDivideStackLevelUpdater)) value = exact<TimesHeroLevelDivideStackLevelUpdater>(source);
		else if(type == typeid(OwnerUpdater)) value = exact<OwnerUpdater>(source);
		else if(type == typeid(CompositeUpdater)) value = exact<CompositeUpdater>(source);
		else throw std::invalid_argument("Unknown owned bonus updater type");
		entry->second = value; updaters.push_back(value);
		if(auto * composite = dynamic_cast<CompositeUpdater *>(value.get()))
			for(auto & child : composite->updaters) child = updater(child);
		if(auto * divided = dynamic_cast<TimesHeroLevelDivideStackLevelUpdater *>(value.get()))
		{
			DividerEdge edges{*this}; divided->serialize(edges); // Only OWNED copy is visited/mutated.
		}
		return value;
	}
	std::shared_ptr<ILimiter> limiter(const TLimiterPtr & source)
	{
		if(!source) return {};
		auto [entry, inserted] = limiterAliases.try_emplace(source.get());
		if(!inserted) return entry->second;
		std::shared_ptr<ILimiter> value;
		const auto & type = typeid(*source);
		if(type == typeid(ILimiter)) value = exact<ILimiter>(source);
		else if(type == typeid(CCreatureTypeLimiter)) value = exact<CCreatureTypeLimiter>(source);
		else if(type == typeid(HasAnotherBonusLimiter)) value = exact<HasAnotherBonusLimiter>(source);
		else if(type == typeid(TerrainLimiter)) value = exact<TerrainLimiter>(source);
		else if(type == typeid(CreatureLevelLimiter)) value = exact<CreatureLevelLimiter>(source);
		else if(type == typeid(FactionLimiter)) value = exact<FactionLimiter>(source);
		else if(type == typeid(CreatureAlignmentLimiter)) value = exact<CreatureAlignmentLimiter>(source);
		else if(type == typeid(OppositeSideLimiter)) value = exact<OppositeSideLimiter>(source);
		else if(type == typeid(RankRangeLimiter)) value = exact<RankRangeLimiter>(source);
		else if(type == typeid(UnitOnHexLimiter)) value = exact<UnitOnHexLimiter>(source);
		else if(type == typeid(UnitAdjacentLimiter)) value = exact<UnitAdjacentLimiter>(source);
		else if(type == typeid(HasChargesLimiter)) value = exact<HasChargesLimiter>(source);
		else if(type == typeid(AllOfLimiter)) value = exact<AllOfLimiter>(source);
		else if(type == typeid(AnyOfLimiter)) value = exact<AnyOfLimiter>(source);
		else if(type == typeid(NoneOfLimiter)) value = exact<NoneOfLimiter>(source);
		else throw std::invalid_argument("Unknown owned bonus limiter type");
		entry->second = value; limiters.push_back(value);
		if(auto * group = dynamic_cast<AggregateLimiter *>(value.get()))
			for(auto & child : group->limiters) child = limiter(child);
		return value;
	}
};
void DividerEdge::operator&(std::shared_ptr<DivideStackLevelUpdater> & value) const
{
	value = std::static_pointer_cast<DivideStackLevelUpdater>(clone.updater(value));
}
}
std::unique_ptr<ownedBonusPayloads::Captured> ownedBonusPayloads::copyOwnedValues(
	const std::vector<std::shared_ptr<const Bonus>> & roots)
{
	validateTypesAndTopology(roots); // Existing finite registry/DAG contract; no parallel type admission.
	Clone clone;
	auto result = std::unique_ptr<Captured>(new Captured);
	for(const auto & root : roots) result->rootValues.push_back(clone.bonus(root));
	result->bonuses = std::move(clone.bonuses);
	result->updaters = std::move(clone.updaters);
	result->limiters = std::move(clone.limiters);
	return result;
}
