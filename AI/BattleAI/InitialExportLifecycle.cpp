/*
 * InitialExportLifecycle.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "InitialExportBattleSubject.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/RoundBonusDuration.h"

void InitialExportBattleSubject::advanceTimedRound(const HypotheticBattle & branch,
	uint32_t id, const CSelector & roundSelector)
{
	captureBranchValues(branch);
	const auto index = occurrence(id);
	// Same pre-expiry subject/polarity query order as the native per-stack hook.
	const auto selected = battle::roundTimedBonusesFor(owned->unitAt(index), roundSelector);
	owned->reduceDurations(owned->sourceAt(index), selected);
}
