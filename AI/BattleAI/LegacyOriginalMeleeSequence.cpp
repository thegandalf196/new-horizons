/*
 * LegacyOriginalMeleeSequence.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequence.h"
#include "InitialExportBattleSubject.h"
#include "MeleeSequenceSupport.h"

namespace heroOrderValue
{
OriginalMeleeSequence projectLegacyOriginalMeleeSequence(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeMovement, BattleInfo & native,
	const BraceMeleeAction & action)
{
	if(!environment || !beforeMovement || !beforeMovement->getBattle()
		|| meleeSequenceSupport::unavailablePolicies(*beforeMovement))
		return projectOriginalMeleeSequence(environment, beforeMovement, action);
	const auto imported = InitialExportBattleSubject::captureLegacyMovement(beforeMovement, native);
	return projectOriginalMeleeSequence(environment, imported, action);
}

OriginalMeleeSequence projectLegacyRecipientMeleeSequence(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeMovement, BattleInfo & native,
	const BraceMeleeAction & action)
{
	// Retain the existing visibility/policy rejection before attempting import.
	if(!environment || !beforeMovement || !beforeMovement->getBattle()
		|| meleeSequenceSupport::unavailablePolicies(*beforeMovement))
		return projectOriginalMeleeSequence(environment, beforeMovement, action);
	const auto imported = InitialExportBattleSubject::captureLegacyMovement(beforeMovement, native);
	return projectRecipientMeleeSequence(environment, imported, action);
}
}
