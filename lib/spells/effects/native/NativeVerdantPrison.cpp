/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"
#include "../../NewHorizonsPlacementPower.h"
#include <cmath>

namespace spells::effects::native
{
namespace
{
class VerdantPrison final : public SummonEffect
{
	struct Share { BattleHex hex; int64_t health; int64_t count; };
	bool enabled(const Mechanics * mechanics) const { return summon::canonical(mechanics, "new-horizons:verdantPrison"); }
	const Creature * type() const { return nativeEvents::creature("core:dendroidGuard"); }
	bool anchor(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		if(!unit || !unit->alive() || !unit->isValidTarget(false) || unit->isInvincible() || !mechanics->isReceptive(unit)) return false;
		return (unit->unitSide() == mechanics->getCasterSide()) == mechanics->isMagicMirror();
	}
	const battle::Unit * resolve(const Mechanics * mechanics, const Target & target) const
	{
		bool identity = false;
		for(const auto & destination : target)
			if(destination.unitValue)
			{
				identity = true;
				const auto units = mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
					{ return unit->unitId() == destination.unitValue->unitId(); });
				if(!units.empty()) return units.front();
			}
		if(identity) return nullptr;
		for(const auto & destination : target)
			if(destination.hexValue.isValid())
			{
				const auto units = mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
					{ return unit->alive() && unit->coversPos(destination.hexValue); });
				if(!units.empty()) return units.front();
			}
		return nullptr;
	}
	const battle::Unit * canonicalTarget(const Mechanics * mechanics, const Target & target) const
	{ return target.size() == 1 ? resolve(mechanics, target) : nullptr; }
	std::vector<BattleHex> ring(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		std::vector<BattleHex> result;
		if(!anchor(mechanics, unit)) return result;
		for(const auto & hex : unit->getSurroundingHexes())
		{
			if(!hex.isAvailable()) continue;
			const auto available = mechanics->battle()->getAvailableHex(type(), mechanics->getCasterSide(), hex);
			if(available.isValid() && available == hex && std::find(result.begin(), result.end(), hex) == result.end()) result.push_back(hex);
		}
		std::sort(result.begin(), result.end(), [](BattleHex left, BattleHex right) { return left.toInt() < right.toInt(); });
		return result;
	}
	int64_t pool(const Mechanics * mechanics) const
	{
		return mechanics->adjustMetamagicMagnitude(newHorizonsMagic::verdantPrisonHitPoints(mechanics->getEffectPower(),
			mechanics->getSpellPowerCoefficientBasisPoints(), mechanics->getEmpowerSpellBonusPercent(),
			summon::perk(mechanics, "new-horizons:natureMagic", "new-horizons:natureMagic.verdantWarden")), nullptr);
	}
	std::vector<Share> allocation(const Mechanics * mechanics, const std::vector<BattleHex> & ring, int64_t health) const
	{
		std::vector<Share> result;
		if(ring.empty() || health <= 0) return result;
		const auto maximum = mechanics->getSummonedCreatureMaxHealth(type(), true);
		if(maximum <= 0) return result;
		const double base = std::floor(static_cast<double>(health) / ring.size());
		const double remainder = static_cast<double>(health) - base * ring.size();
		for(size_t index = 0; index < ring.size(); ++index)
		{
			const double share = base + (index + 1 <= remainder ? 1 : 0);
			if(share > 0) result.push_back({ring[index], static_cast<int64_t>(share), static_cast<int64_t>(std::ceil(share / maximum))});
		}
		return result;
	}
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(enabled(mechanics) && !mechanics->isMagicMirror() && !mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
			{ return unit->unitSide() != mechanics->getCasterSide() && !ring(mechanics, unit).empty(); }).empty()) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		if(enabled(mechanics))
		{
			const auto * unit = canonicalTarget(mechanics, target);
			if(anchor(mechanics, unit) && !ring(mechanics, unit).empty()) return true;
		}
		return status::reject(mechanics, &problem);
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		if(!enabled(mechanics)) return {};
		const auto * unit = resolve(mechanics, target.empty() ? aim : target);
		return anchor(mechanics, unit) && !ring(mechanics, unit).empty() ? Target{Destination(unit)} : Target{};
	}
	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & target) const override
	{
		if(!enabled(mechanics)) return;
		const auto result = ring(mechanics, resolve(mechanics, target));
		hexes.clear();
		for(const auto hex : result) hexes.insert(hex);
	}
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		if(!enabled(mechanics)) return {};
		const auto surrounding = ring(mechanics, canonicalTarget(mechanics, target));
		const auto health = pool(mechanics);
		int64_t count = 0;
		for(const auto & share : allocation(mechanics, surrounding, health)) count += share.count;
		return surrounding.empty() || count == 0 ? SpellEffectValue{} : summon::value(health, count, type());
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!enabled(mechanics)) return;
		const auto surrounding = ring(mechanics, canonicalTarget(mechanics, target));
		const auto health = pool(mechanics);
		const auto shares = allocation(mechanics, surrounding, health);
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(const auto & share : shares)
		{
			battle::UnitInfo info;
			info.count = share.count; info.type = type()->getId(); info.side = mechanics->getCasterSide();
			info.position = share.hex; info.summoned = true; info.natureSummoned = true;
			if(const auto * unit = nativeEvents::addUnit(context, info))
			{
				auto excess = unit->getAvailableHealth() - share.health;
				if(excess <= 0) continue;
				auto copy = unit->acquireState(); copy->damage(excess);
				nativeEvents::changeUnit(context, *copy);
			}
		}
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createVerdantPrisonEffect(const std::string & path)
{
	if(path == "spells/verdantPrison") return std::make_shared<spells::effects::native::VerdantPrison>();
	return {};
}
}
