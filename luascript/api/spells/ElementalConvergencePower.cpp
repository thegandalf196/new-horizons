/*
 * ElementalConvergencePower.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ElementalConvergencePower.h"
#include "Mechanics.h"

#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsElementalConvergencePower.h"

namespace scripting::api
{
namespace
{
int64_t currentElementalConvergenceHitPoints(const ::spells::Mechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	const bool conjurer = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::NATURE_MAGIC_SKILL), "new-horizons:natureMagic.elementalConjurer");
	return newHorizonsMagic::elementalConvergenceHitPoints(mechanics.getEffectPower(),
		mechanics.getSpellPowerCoefficientBasisPoints(), mechanics.getWarcastingBonusPercent(),
		mechanics.getEmpowerSpellBonusPercent(), conjurer);
}
}

void registerElementalConvergenceMethods(MethodRegistrar & registrar)
{
	registrar.function<&currentElementalConvergenceHitPoints>("getElementalConvergenceHitPoints", {},
		"Returns this cast's Elemental Convergence HP budget with Conjurer and both floors. "
		"Does not choose a terrain creature, count, landing position or targets. No RNG.");
}
}
