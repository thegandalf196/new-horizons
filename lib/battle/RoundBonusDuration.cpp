/*
 * RoundBonusDuration.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "RoundBonusDuration.h"
#include "TimedEffectValueTransitions.h"
#include "NewHorizonsPlague.h"
#include "../spells/NewHorizonsSorcery.h"
#include "Unit.h"

CSelector battle::roundTimedBonuses(const JsonNode & savedRules)
{
	const auto plagueMarker = newHorizonsPlague::markerSelector(savedRules);
	// Plague advances after its end-of-stack-turn tick, not at the round clock.
	return CSelector([plagueMarker](const Bonus * bonus)
	{
		return bonus && timedEffects::roundClock(*bonus, plagueMarker(bonus));
	});
}
