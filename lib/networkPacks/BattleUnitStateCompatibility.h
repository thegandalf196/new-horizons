/*
 * BattleUnitStateCompatibility.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "BattleChanges.h"

// Write-side envelope composition only; callers retain saving guards and prefixes.
// Frozen feedback stays envelope-owned at its original point before form checks.
namespace battleUnitStateCompatibility
{
template <typename Handler, typename FrozenFeedback>
void validateForEnvelope(const UnitChanges & change, Handler & h,
	const FrozenFeedback & validateFrozenFeedback)
{
	change.validateOverwatchSerialization(h);
	change.validateVeteranCohesionSerialization(h);
	change.validateTrainingSerialization(h);
	change.validatePlagueSerialization(h);
	change.validateFrozenSerialization(h);
	validateFrozenFeedback();
	change.validateBattleFormSerialization(h);
	change.validateConfusionSerialization(h);
	change.validateMoraleActivationSerialization(h);
	change.validateOpportunistSerialization(h);
}

template <typename Handler>
void validateForEnvelope(const UnitChanges & change, Handler & h)
{
	validateForEnvelope(change, h, []() {});
}
}
