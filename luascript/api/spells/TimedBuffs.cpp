/*
 * TimedBuffs.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "TimedBuffs.h"
#include "Mechanics.h"
#include "../../../lib/spells/NewHorizonsTimedBuffMagnitude.h"
#include "../../../lib/spells/NewHorizonsSorceryRevision.h"
#include <vcmi/spells/Spell.h>
namespace scripting::api
{
namespace
{
std::optional<double> currentCasterEnchantValue(const ::spells::Mechanics & m, double value)
{
	const auto * family = MechanicsProxy::getEffectSpell(m);
	return family ? newHorizonsMagic::casterEnchantValueOverride(m.getHeroCaster(), family->getId(), value) : std::nullopt;
}
int32_t currentSlowReduction(const ::spells::Mechanics & m)
{
	return -newHorizonsSorceryRevision::slowBaseReduction(m.getEffectPower(), m.getSpellPowerCoefficientBasisPoints(),
		m.getWarcastingBonusPercent(), m.getEmpowerSpellBonusPercent());
}
double currentSlowVariantValue(const ::spells::Mechanics &, double value, int32_t percent)
{
	return newHorizonsSorceryRevision::scaleSlowVariantValue(value, percent);
}
}
void registerTimedBuffMethods(MethodRegistrar & R)
{
	R.function<&currentCasterEnchantValue>("applyCasterEnchantValue", {{"value", "Value after recipient-tier specialties."}}, {},
		"Returns the caster's existing first additive enchantment then fixed override, or nil when neither exists; original timed-spell order.");
	R.function<&currentSlowReduction>("getSlowBaseReduction", {},
		"Returns the negative revised Slow magnitude before recipient specialties and late variant scaling.");
	R.function<&currentSlowVariantValue>("scaleSlowVariantValue", {{"value", "Final recipient magnitude."}, {"percent", "Saved late variant percentage."}}, {},
		"Scales negative Slow values toward zero after all specialties, preserving nonnegative values and full-power variants.");
}
}
