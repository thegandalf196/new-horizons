/*
 * RoundBonusDuration.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "RoundBonusDuration.h"
#include "NewHorizonsPlague.h"
#include "../spells/NewHorizonsSorcery.h"
#include "Unit.h"

CSelector battle::roundTimedBonuses(const JsonNode & savedRules)
{
	const auto plagueMarker = newHorizonsPlague::markerSelector(savedRules);
	// Plague advances after its end-of-stack-turn tick, not at the round clock.
	return CSelector(Bonus::NTurns).And(plagueMarker.Not());
}

CSelector battle::roundTimedBonusesFor(const Unit & unit, const CSelector & base)
{
	const auto preserveBeneficial = newHorizonsSorcery::spellLockPreservesBeneficial(unit);
	if(newHorizonsSorcery::isSpellLocked(unit) && preserveBeneficial.has_value())
	{
		const auto preservedMagic = CSelector([preserveBeneficial](const Bonus * bonus)
		{
			return newHorizonsSorcery::isPreservedSpellLockEffect(bonus, *preserveBeneficial);
		});
		return base.And(preservedMagic.Not());
	}
	return base;
}
