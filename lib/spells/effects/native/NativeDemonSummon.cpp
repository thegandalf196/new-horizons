/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"
#include <cmath>

namespace spells::effects::native
{
namespace
{
class DemonSummon final : public UnitEffect
{
	const Creature * type() const { return nativeEvents::creature(parameters["id"].String()); }
	int64_t amount(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		const double maximum = type()->getMaxHealth();
		const double fromHealth = std::floor(static_cast<double>(unit->getTotalHealth()) / maximum);
		const double fromPower = std::floor(static_cast<double>(mechanics->applySpellBonus(mechanics->getEffectValue(), unit)) / maximum);
		return static_cast<int64_t>(std::min({fromHealth, static_cast<double>(unit->unitBaseAmount()), fromPower}));
	}
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		if(summon::phantomCaster(mechanics) || unit->getPhantomInitialIntegrity() > 0 || !unit->isDead()) return false;
		for(const auto & hex : unit->getHexes())
			if(!mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * other)
				{ return other->isValidTarget(false) && other->coversPos(hex); }).empty()) return false;
		return !unit->isGhost() && amount(mechanics, unit) != 0 && mechanics->isReceptive(unit);
	}
public:
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(summon::phantomCaster(mechanics)) return;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!unit || unit->getPhantomInitialIntegrity() > 0 || !unit->isDead() || unit->isGhost()) break;
			const auto hex = context.battle.getAvailableHex(type(), mechanics->getCasterSide(), unit->getPosition());
			if(!hex.isValid()) break;
			const auto raised = amount(mechanics, unit);
			if(raised < 1) break;
			battle::UnitInfo info;
			info.count = raised; info.type = type()->getId(); info.side = mechanics->getCasterSide();
			info.position = hex; info.summoned = !parameters["permanent"].Bool();
			nativeEvents::addUnit(context, info);
			nativeEvents::removeUnit(context, *unit);
		}
	}
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		if(summon::phantomCaster(mechanics)) return {};
		for(const auto & destination : target)
			if(const auto * unit = destination.unitValue)
				return unit->getPhantomInitialIntegrity() > 0 ? SpellEffectValue{} : summon::value(0, amount(mechanics, unit), type());
		return {};
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createDemonSummonEffect(const std::string & path)
{
	if(path == "spells/demonSummon") return std::make_shared<spells::effects::native::DemonSummon>();
	return {};
}
}
