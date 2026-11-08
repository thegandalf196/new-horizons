/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"

namespace spells::effects::native
{
namespace
{
class PhantomArmy final : public UnitEffect
{
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		return unit && unit->alive() && unit->isValidTarget(false) && !unit->isClone()
			&& unit->getPhantomInitialIntegrity() <= 0 && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& mechanics->ownerMatches(unit) && mechanics->isReceptive(unit);
	}
	BattleHex landing(const Mechanics * mechanics, const battle::Unit * unit) const
	{ return mechanics->battle()->getAvailableHex(summon::creature(unit), mechanics->getCasterSide(), unit->getPosition()); }
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(!mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
			{ return isValidTarget(mechanics, unit) && landing(mechanics, unit).isValid(); }).empty()) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		for(const auto & destination : target)
			if(isValidTarget(mechanics, destination.unitValue) && landing(mechanics, destination.unitValue).isValid()) return true;
		return status::reject(mechanics, &problem);
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		auto result = UnitEffect::transformTarget(mechanics, aim, target);
		if(result.size() > 1) result.resize(1);
		return result;
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(const auto & destination : target)
		{
			const auto * source = destination.unitValue;
			if(!isValidTarget(mechanics, source)) continue;
			const auto position = landing(mechanics, source);
			if(!position.isValid()) continue;
			battle::UnitInfo info;
			info.count = source->getCount(); info.type = source->creatureId(); info.side = mechanics->getCasterSide();
			info.position = position; info.summoned = true; info.phantomIntegrity = mechanics->getPhantomArmyIntegrity(source);
			info.phantomDuration = mechanics->adjustMetamagicDuration(mechanics->adjustEffectDuration(2), source);
			if(nativeEvents::addUnit(context, info)) server->recordResolvedSpellRecipient(source->unitId());
		}
	}
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		const auto * unit = target.empty() ? nullptr : target.front().unitValue;
		return isValidTarget(mechanics, unit) ? summon::value(mechanics->getPhantomArmyIntegrity(unit), unit->getCount(), summon::creature(unit)) : SpellEffectValue{};
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createPhantomArmyEffect(const std::string & path)
{
	if(path == "spells/phantomArmy") return std::make_shared<spells::effects::native::PhantomArmy>();
	return {};
}
}
