/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"
#include <cmath>

namespace spells::effects::native
{
namespace
{
class Reanimate final : public UnitEffect
{
	bool enabled(const Mechanics * mechanics) const { return summon::canonical(mechanics, "new-horizons:reanimate"); }
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		if(!enabled(mechanics) || !mechanics->getHeroCaster() || !unit || !unit->isValidTarget(true)
			|| unit->isGhost() || unit->isClone() || unit->getPhantomInitialIntegrity() > 0 || !mechanics->ownerMatches(unit)) return false;
		const double health = unit->getCount() > 0 ? static_cast<double>(unit->getCount() - 1) * unit->getMaxHealth() + unit->getFirstHPleft() : 0;
		const double injuries = unit->getTotalHealth() - health - static_cast<double>(unit->getUnusableRemains()) * unit->getMaxHealth();
		if(injuries <= 0) return false;
		if(unit->isDead())
			for(const auto & hex : unit->getHexes())
				if(!mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * other)
					{ return other != unit && other->isValidTarget(false) && other->coversPos(hex); }).empty()) return false;
		return true;
	}
	int64_t amount(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		double pool = mechanics->getEffectValue();
		if(summon::perk(mechanics, "new-horizons:shadowMagic", "new-horizons:shadowMagic.reanimator"))
		{
			const double wounds = unit->alive() ? std::max(0.0, static_cast<double>(unit->getMaxHealth()) - unit->getFirstHPleft()) : 0;
			pool += std::floor(std::max(0.0, pool - wounds) / 4);
		}
		return mechanics->adjustMetamagicMagnitude(static_cast<int64_t>(pool), unit);
	}
	bool validPair(const Mechanics * mechanics, const Target & target) const
	{
		return enabled(mechanics) && mechanics->getHeroCaster() && target.size() == 1
			&& isValidTarget(mechanics, target.front().unitValue) && isReceptive(mechanics, target.front().unitValue);
	}
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(enabled(mechanics) && mechanics->getHeroCaster() && !mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
			{ return isValidTarget(mechanics, unit) && isReceptive(mechanics, unit); }).empty()) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{ return validPair(mechanics, target) || status::reject(mechanics, &problem); }
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target &) const override
	{ return enabled(mechanics) && aim.size() == 1 && aim.front().unitValue ? aim : Target{}; }
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		SpellEffectValue result;
		if(!enabled(mechanics)) return result;
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!unit || !isValidTarget(mechanics, unit) || !isReceptive(mechanics, unit)) continue;
			auto state = unit->acquireState();
			auto health = amount(mechanics, unit);
			const auto healed = state->heal(health, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
			result.hpDelta += healed.healedHealthPoints; result.unitsDelta += healed.resurrectedCount;
			result.unitType = summon::creature(unit);
		}
		return result;
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!validPair(mechanics, target)) return;
		const auto * unit = target.front().unitValue;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		const auto [health, count] = nativeEvents::heal(context, *unit, amount(mechanics, unit), EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
		if(health <= 0 && count <= 0) return;
		MetaString message;
		message.appendTextID(count > 0 ? "new-horizons.combat.reanimate.restored" : "new-horizons.combat.reanimate.healed");
		message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount())); message.replaceNumber(health);
		if(count > 0) message.replaceNumber(count);
		nativeEvents::log(context, message);
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createReanimateEffect(const std::string & path)
{
	if(path == "spells/reanimate") return std::make_shared<spells::effects::native::Reanimate>();
	return {};
}
}
