/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"
#include "../../RecipientSpellEffectPreview.h"
#include <cmath>

namespace spells::effects::native
{
namespace
{
class HydrasVitality final : public UnitEffect
{
	bool enabled(const Mechanics * mechanics) const { return summon::canonical(mechanics, "new-horizons:hydrasVitality"); }
	std::optional<int32_t> maximum(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		if(!unit) return {};
		const auto copy = unit->acquireState();
		const double reference = copy->getCapacityHealthReferenceMax();
		const int64_t ordinary = std::max<int64_t>(25'000'000, std::min<int64_t>(50'000'000, mechanics->getEffectValue()));
		const double percent = mechanics->adjustMetamagicMagnitude(ordinary, unit);
		const double scaled = std::floor(reference / 100'000'000.0);
		const double remainder = reference - scaled * 100'000'000.0;
		const double increase = scaled * percent + std::floor(remainder * percent / 100'000'000.0);
		const double result = reference + increase;
		if(result > 2147483647.0) return {};
		return static_cast<int32_t>(result);
	}
	bool eligible(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		return unit && unit->alive() && unit->isValidTarget(false) && unit->isLiving() && !unit->isClone()
			&& unit->getPhantomInitialIntegrity() <= 0 && mechanics->ownerMatches(unit, true)
			&& maximum(mechanics, unit).has_value() && isReceptive(mechanics, unit);
	}
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{ return enabled(mechanics) && eligible(mechanics, unit); }
	bool validPair(const Mechanics * mechanics, const Target & target) const
	{ return enabled(mechanics) && target.size() == 1 && eligible(mechanics, target.front().unitValue); }
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(enabled(mechanics) && !mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit) { return eligible(mechanics, unit); }).empty()) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{ return validPair(mechanics, target) || status::reject(mechanics, &problem); }
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		if(!enabled(mechanics)) return {};
		auto result = transformByRange(mechanics, aim, target);
		return result.size() == 1 && eligible(mechanics, result.front().unitValue) ? result : Target{};
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!validPair(mechanics, target)) return;
		const auto * unit = target.front().unitValue;
		const auto targetMaximum = maximum(mechanics, unit);
		if(!targetMaximum) return;
		auto copy = unit->acquireState();
		const auto reference = copy->getCapacityHealthReferenceMax();
		copy->preserveCreatureHealthOnCapacityIncrease();
		if(reference <= 0) return;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		nativeEvents::changeUnit(context, *copy);
		const auto duration = newHorizonsMagic::scriptedRoundStatusDuration(*mechanics).value_or(0);
		const auto key = mechanics->getSpell()->getJsonKey();
		auto capacity = status::bonus("STACK_HEALTH", *targetMaximum, key, duration);
		capacity["valueType"].String() = "INDEPENDENT_MAX";
		capacity["description"].String() = "New Horizons: Hydra's Vitality";
		status::add(context, *unit, std::move(capacity));
		auto regeneration = status::bonus("HP_REGENERATION", 0, key, duration);
		regeneration["description"].String() = "New Horizons: Hydra's Vitality";
		status::add(context, *unit, std::move(regeneration));
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createNatureHealthEffect(const std::string & path)
{
	if(path == "spells/hydrasVitality") return std::make_shared<spells::effects::native::HydrasVitality>();
	return {};
}
}
