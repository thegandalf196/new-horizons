/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"
#include <cmath>

namespace spells::effects::native
{
namespace
{
class Summon final : public SummonEffect
{
	const Creature * type() const { return nativeEvents::creature(parameters["id"].String()); }
	int64_t effect(const Mechanics * mechanics) const
	{
		const auto raw = mechanics->calculateRawEffectValue(0, mechanics->getEffectPower());
		return mechanics->applySpecificSpellBonus(mechanics->scaleSpellPowerComponentWithCoefficientBasisPoints(raw,
			mechanics->getEffectPowerDivisor(), mechanics->getSpellPowerCoefficientBasisPoints()));
	}
	int64_t health(const Mechanics * mechanics) const
	{
		const double value = effect(mechanics);
		return static_cast<int64_t>(parameters["summonByHealth"].Bool() ? value : value * type()->getMaxHealth());
	}
	int64_t count(const Mechanics * mechanics) const
	{
		const double value = effect(mechanics);
		return static_cast<int64_t>(parameters["summonByHealth"].Bool() ? std::floor(value / type()->getMaxHealth()) : value);
	}
	bool matching(const Mechanics * mechanics, const battle::Unit * unit, bool sameType) const
	{
		return unit->unitOwner() == mechanics->getCasterColor() && unit->isSummoned() && !unit->isClone()
			&& ((summon::creature(unit)->getJsonKey() == type()->getJsonKey()) == sameType);
	}
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(summon::phantomCaster(mechanics) || count(mechanics) == 0)
		{ mechanics->adaptGenericProblem(problem); return false; }
		if(!parameters["exclusive"].Bool()) return true;
		const auto others = mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit) { return matching(mechanics, unit, false); });
		if(others.empty()) return true;
		MetaString message;
		message.appendTextID("core.genrltxt.538");
		if(const auto * hero = mechanics->getHeroCaster())
		{
			message.replaceTextID(hero->getNameTextID());
			message.replaceTextID(nativeEvents::creatureName(others.front(), 0));
			message.replaceTextID(hero->gender == EHeroGender::FEMALE ? "core.genrltxt.540" : "core.genrltxt.539");
		}
		problem.add(std::move(message));
		return false;
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target &) const override
	{
		if(!summon::phantomCaster(mechanics)) return true;
		mechanics->adaptGenericProblem(problem); return false;
	}
	Target filterTarget(const Mechanics * mechanics, const Target & target) const override
	{ return summon::phantomCaster(mechanics) ? Target{} : target; }
	Target transformTarget(const Mechanics * mechanics, const Target &, const Target &) const override
	{
		if(summon::phantomCaster(mechanics)) return {};
		const auto same = mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
			{ return matching(mechanics, unit, true) && unit->alive(); });
		if(!same.empty() && parameters["summonSameUnit"].Bool()) return {Destination(same.front())};
		const auto hex = mechanics->battle()->getAvailableHex(type(), mechanics->getCasterSide());
		return hex.isValid() ? Target{Destination(hex)} : Target{};
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(summon::phantomCaster(mechanics)) return;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		const bool permanent = parameters["permanent"].Bool();
		for(const auto & destination : target)
		{
			if(destination.unitValue)
				nativeEvents::heal(context, *destination.unitValue, health(mechanics), EHealLevel::OVERHEAL,
					permanent ? EHealPower::PERMANENT : EHealPower::ONE_BATTLE);
			else
			{
				battle::UnitInfo info;
				info.count = count(mechanics); info.type = type()->getId(); info.side = mechanics->getCasterSide();
				info.position = destination.hexValue; info.summoned = !permanent;
				if(!permanent)
					for(const auto & school : context.battle.battleGetSpellSchools(mechanics->getSpellId()))
						if(school.serializationKey() == "new-horizons:nature") { info.natureSummoned = true; break; }
				nativeEvents::addUnit(context, info);
			}
		}
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createSummonEffect(const std::string & path)
{
	if(path == "spells/summon") return std::make_shared<spells::effects::native::Summon>();
	return {};
}
}
