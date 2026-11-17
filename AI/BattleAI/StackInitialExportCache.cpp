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
	// Native scratch/count transitions can precede outer publication. The
	// owned programme keys Count+leaf internally; legacy proxy version alone
	// does not certify those changes. Query its immutable frame directly.
	return !modifierBaseProgram && !ownedQuerySource && !owner->initialExportSubject();
}
