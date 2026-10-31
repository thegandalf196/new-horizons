/*
 * TimeStopRadius.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "TimeStopRadius.h"
#include "Mechanics.h"

#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"

namespace scripting::api
{
namespace
{
int32_t currentTimeStopRadius(const ::spells::Mechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	const bool chronomancer = hero && hero->hasActivePerk(
		newHorizonsSorcery::SORCERY_MAGIC_SKILL, newHorizonsSorcery::CHRONOMANCER_PERK);
	return newHorizonsSorcery::timeStopRadius(mechanics.getEffectPower(),
		mechanics.getSpellPowerCoefficientBasisPoints(), mechanics.getWarcastingBonusPercent(),
		mechanics.getEmpowerSpellBonusPercent(), chronomancer);
}
}

void registerTimeStopRadiusMethods(MethodRegistrar & registrar)
{
	registrar.function<&currentTimeStopRadius>("getTimeStopRadius", {},
		"Returns this cast's Time Stop radius from its captured coefficient, Warcasting, Empower "
		"and active Chronomancer, with one final floor and the ordinary radius cap. No RNG or targets.");
}
}
