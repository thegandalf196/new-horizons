/*
 * UnitUpdateAdmission.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "UnitUpdateAdmission.h"
#include "Unit.h"

battle::UnitUpdateGate battle::classifyUnitUpdateGate(const Unit & unit, int64_t healthDelta)
{
	if(unit.isTimeStopped() && healthDelta != 0)
		return UnitUpdateGate::IGNORE_HEALTH_STOP;
	if(unit.isTimeStopped())
		return UnitUpdateGate::IGNORE_STATE_STOP;
	if(!unit.alive() && healthDelta > 0)
		return UnitUpdateGate::CHECK_RESURRECTION_SPACE;
	return UnitUpdateGate::APPLY;
}
