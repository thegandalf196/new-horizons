/*
 * StackSerializationChecks.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../CStack.h"

// Format-independent CStack save guards called by the CStack.h serializer.

void CStack::validateTimedSummonSerialization() const
{
	if(summonRoundsRemaining > 0)
		throw std::runtime_error("Cannot save a timed summon without its unit health state");
}

void CStack::validateVeteranDamageHistorySerialization(bool supported) const
{
	if(veteranPhysicalDamageSinceActivation < 0)
		throw std::runtime_error("Invalid negative Veteran damage accumulator");
	if(!supported && veteranPhysicalDamageSinceActivation != 0)
		throw std::runtime_error("Cannot discard Veteran damage history in an older stack format");
}
