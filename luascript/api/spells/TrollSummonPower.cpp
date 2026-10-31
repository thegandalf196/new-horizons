/*
 * TrollSummonPower.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "TrollSummonPower.h"
#include "Mechanics.h"

#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsTrollSummonPower.h"

namespace scripting::api
{
namespace
{
int64_t currentTrollSummonHitPoints(const ::spells::Mechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	const bool beastcaller = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::NATURE_MAGIC_SKILL), "new-horizons:natureMagic.beastcaller");
	return newHorizonsMagic::trollSummonHitPoints(mechanics.getEffectPower(),
		mechanics.getSpellPowerCoefficientBasisPoints(), mechanics.getWarcastingBonusPercent(),
		mechanics.getEmpowerSpellBonusPercent(), beastcaller);
}
}

void registerTrollSummonMethods(MethodRegistrar & registrar)
{
	registrar.function<&currentTrollSummonHitPoints>("getTrollSummonHitPoints", {},
		"Returns this cast's exact Troll summon HP budget, preserving Beastcaller's whole-pool "
		"scaling and both floors. Creature count and landing legality are separate. No RNG.");
}
}
