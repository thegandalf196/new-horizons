/*
 * StackInitialExportCache.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"

bool StackWithBonuses::isBonusCacheable() const
{
	return !owner->initialExportSubject();
}
