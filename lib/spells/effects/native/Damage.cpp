/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Damage.h"
#include "../../ISpellMechanics.h"
#include "../../../battle/CUnitState.h"
#include "../../../bonuses/BonusList.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include <vcmi/spells/Spell.h>
#include <cmath>

#include <typeinfo>

namespace spells::effects::native
{
namespace
{
std::optional<int> conductor(const Mechanics & mechanics, size_t index)
{
	if(index == 0 || index >= 5) return std::nullopt;
	const auto * hero = mechanics.getHeroCaster();
	const auto * spell = mechanics.getSpell();
	if(!hero || !spell || !hero->hasActivePerk("new-horizons:havocMagic", "new-horizons:havocMagic.conductor")) return std::nullopt;
	const auto key = spell->getJsonKey();
	if(key != "core:chainLightning" && key != "new-horizons:masterChainLightning") return std::nullopt;
	constexpr std::array percent{100, 75, 55, 40, 30};
	return percent[index];
}
}

ResistanceShareKind Damage::resistanceShareKind() const
{
	return typeid(*this) == typeid(Damage) ? ResistanceShareKind::Damage : ResistanceShareKind::Unsupported;
}

bool Damage::isReceptive(const Mechanics * mechanics, const battle::Unit * unit) const
{
	const auto * spell = mechanics->getSpell();
	const bool independent = spell->isMagical() && mechanics->usesNewHorizonsMultiplicativeMDR();
	if(spell->isMagical() && !independent
		&& unit->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION, SpellSchool::ANY) >= 100) return false;
	const auto reductions = unit->getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION);
	bool immune = false;
	spell->forEachSchool([&](const SpellSchool & school, bool & stop)
	{
		double total = 0;
		for(const auto & bonus : *reductions)
			if(bonus->subtype != BonusSubtypeID(SpellSchool::ANY) && bonus->subtype == BonusSubtypeID(school)) total += bonus->val;
		if(total >= 100 && !independent) immune = stop = true;
	});
	return !immune && UnitEffect::isReceptive(mechanics, unit);
}

int64_t Damage::damageForTarget(size_t index, const Mechanics * mechanics, const battle::Unit * unit) const
{
	double base;
	const bool percentage = parameters["killByPercentage"].Bool();
	const bool count = parameters["killByCount"].Bool();
	if(percentage) base = std::floor(static_cast<double>(unit->getCount()) * mechanics->getEffectValue() / 100) * unit->getMaxHealth();
	else if(count) base = static_cast<double>(mechanics->getEffectValue()) * unit->getMaxHealth();
	else base = static_cast<double>(mechanics->adjustEffectValue(unit));
	const double integrity = static_cast<double>(unit->getPhantomIntegrity());
	if((percentage || count) && mechanics->getSpell()->isMagical() && integrity > 0)
		base = base >= integrity / 2 ? integrity : base * 2;
	if(parameters["chainLength"].Integer() > 1 && index > 0)
	{
		double chain = parameters["chainFactor"].Float();
		const double perLevel = parameters["chainFactorPerHeroLevel"].Float();
		if(perLevel != 0)
		{
			const auto * hero = mechanics->getHeroCaster();
			chain += perLevel * std::max(0, hero ? static_cast<int>(hero->level) : 1);
			if(!parameters["chainFactorMaximum"].isNull()) chain = std::min(chain, parameters["chainFactorMaximum"].Float());
		}
		int retention = mechanics->getNewHorizonsChainLightningRetentionPercent(static_cast<int>(index));
		const auto conducting = conductor(*mechanics, index);
		if(retention >= 0)
		{
			if(conducting) retention = std::max(retention, *conducting);
			base = std::floor(base * retention / 100);
		}
		else
		{
			double multiplier = std::pow(chain, static_cast<double>(index));
			if(conducting) multiplier = std::max(multiplier, *conducting / 100.0);
			base = std::floor(multiplier * base);
		}
	}
	const auto amount = static_cast<int64_t>(base);
	return indirect ? amount : mechanics->adjustDirectCreatureActivationDamage(amount);
}

SpellEffectValue Damage::getHealthChange(const Mechanics * mechanics, const Target & target) const
{
	SpellEffectValue result;
	for(size_t index = 0; index < target.size(); ++index)
	{
		const auto * unit = target[index].unitValue;
		if(!unit || !unit->alive()) continue;
		auto amount = damageForTarget(index, mechanics, unit);
		auto copy = unit->acquireState();
		const auto before = copy->getAvailableHealth();
		const auto count = copy->getCount();
		copy->damage(amount);
		result.hpDelta -= before - copy->getAvailableHealth();
		result.unitsDelta -= count - copy->getCount();
	}
	return result;
}
}
