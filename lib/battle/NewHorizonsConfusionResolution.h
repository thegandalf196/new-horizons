/*
 * NewHorizonsConfusionResolution.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "NewHorizonsConfusion.h"
#include "NewHorizonsConfusionState.h"

namespace newHorizonsConfusion
{
/// One resolved outcome with its unconditional (or Confounder-conditioned)
/// probability. Target pointers belong to the supplied battle snapshot.
struct DLL_LINKAGE Outcome
{
	battle::ConfusionBehavior behavior = battle::ConfusionBehavior::DEFEND;
	AttackChoice action;
	double probability = 0.0;
};

/// Equal raw behavior weights, then uniform enemy groups, then uniform legal
/// positions. Impossible Attack and trapped Wander resolve as ordinary Defend.
/// Confounder conditions on a different resolved behavior only if one exists.
DLL_LINKAGE std::vector<Outcome> enumerateOutcomes(const Choices & choices,
	battle::ConfusionBehavior previous, bool confounder);
DLL_LINKAGE std::vector<Outcome> enumerateOutcomes(const CBattleInfoCallback & battle,
	const battle::Unit * unit, battle::ConfusionBehavior previous, bool confounder);
/// Select using an authoritative uniform draw in [0, 1); enumeration never
/// consumes RNG and is shared by detached expected-value forecasts.
DLL_LINKAGE const Outcome & selectOutcome(const std::vector<Outcome> & outcomes, double draw);
}
