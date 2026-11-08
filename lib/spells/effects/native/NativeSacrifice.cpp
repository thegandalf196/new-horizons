/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"

namespace spells::effects::native
{
namespace
{
class Sacrifice final : public UnitEffect
{
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{ return !summon::phantomCaster(mechanics) && unit->getPhantomInitialIntegrity() <= 0 && unit->isValidTarget(true); }
	bool validPair(const Mechanics * mechanics, const Target & target) const
	{
		if(summon::phantomCaster(mechanics) || target.empty()) return false;
		const auto * dead = target.front().unitValue;
		if(!dead || dead->alive() || !isValidTarget(mechanics, dead)) return false;
		if(target.size() == 1) return true;
		if(target.size() != 2) return false;
		const auto * victim = target[1].unitValue;
		return victim && victim->alive() && isValidTarget(mechanics, victim) && mechanics->isReceptive(victim) && mechanics->ownerMatches(victim);
	}
	int64_t healing(const Mechanics * mechanics, const battle::Unit * victim) const
	{
		const double count = victim->getCount();
		const double fixed = static_cast<double>(victim->getMaxHealth()) + mechanics->calculateRawEffectValue(0, 1);
		const auto scaled = mechanics->scaleSpellPowerComponentWithCoefficientBasisPoints(
			static_cast<int64_t>(mechanics->getEffectPower() * count), mechanics->getEffectPowerDivisor(), mechanics->getSpellPowerCoefficientBasisPoints());
		return static_cast<int64_t>(fixed * count + scaled);
	}
	EHealLevel level() const
	{
		const auto & name = parameters["healLevel"].String();
		return name == "heal" ? EHealLevel::HEAL : name == "overHeal" ? EHealLevel::OVERHEAL : EHealLevel::RESURRECT;
	}
	EHealPower power() const
	{ return parameters["healPower"].String() == "oneBattle" ? EHealPower::ONE_BATTLE : EHealPower::PERMANENT; }
public:
	void adjustTargetTypes(std::vector<TargetType> & types, const Mechanics *) const override
	{
		if(types.empty()) return;
		if(types.front() != AimType::CREATURE) { types.clear(); return; }
		if(types.size() == 1) types.push_back(AimType::CREATURE);
		else if(types[1] != AimType::CREATURE) types.clear();
	}
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(summon::phantomCaster(mechanics)) return status::reject(mechanics, &problem);
		const auto units = mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
			{ return isValidTarget(mechanics, unit) && mechanics->isReceptive(unit) && mechanics->ownerMatches(unit); });
		bool dead = false, alive = false;
		for(const auto * unit : units)
		{
			dead |= unit->isDead(); alive |= unit->alive();
			if(dead && alive) return true;
		}
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem &, const Mechanics * mechanics, const Target & target) const override
	{ return validPair(mechanics, target); }
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		if(summon::phantomCaster(mechanics)) return {};
		const auto filtered = UnitEffect::transformTarget(mechanics, aim,
			!aim.empty() && aim.front().unitValue ? Target{aim.front()} : target);
		if(filtered.empty()) return {};
		Target result{filtered.front()};
		if(aim.size() >= 2) result.push_back(aim[1]);
		return result;
	}
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		if(summon::phantomCaster(mechanics) || target.empty()) return {};
		const auto * unit = target.front().unitValue;
		if(!unit || unit->getPhantomInitialIntegrity() > 0) return {};
		if(unit->alive()) return summon::value(healing(mechanics, unit), -unit->getCount(), summon::creature(unit));
		return summon::value(static_cast<int64_t>(static_cast<double>(unit->unitBaseAmount()) * unit->getMaxHealth()), unit->unitBaseAmount(), summon::creature(unit));
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(target.size() != 2 || !validPair(mechanics, target)) return;
		const auto * dead = target.front().unitValue, * victim = target[1].unitValue;
		const auto amount = mechanics->adjustMetamagicMagnitude(healing(mechanics, victim), dead);
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		const auto [health, count] = nativeEvents::heal(context, *dead, amount, level(), power());
		nativeEvents::removeUnit(context, *victim);
		if(count <= 0) return;
		MetaString message;
		message.appendTextID(count == 1 ? "core.genrltxt.117" : "core.genrltxt.116");
		message.replaceTextID(nativeEvents::creatureName(dead, dead->getCount())); message.replaceNumber(count);
		nativeEvents::log(context, message);
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createSacrificeEffect(const std::string & path)
{
	if(path == "spells/sacrifice") return std::make_shared<spells::effects::native::Sacrifice>();
	return {};
}
}
