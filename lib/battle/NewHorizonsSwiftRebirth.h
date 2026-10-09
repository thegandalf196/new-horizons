/*
 * NewHorizonsSwiftRebirth.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "Unit.h"
#include <optional>

namespace newHorizonsSwiftRebirth
{
struct DLL_LINKAGE Lifecycle
{
	int32_t birthRound = 0;
	bool priorityPending = true;
	bool normalCompleted = false;
};

DLL_LINKAGE Bonus marker(int32_t birthRound);
DLL_LINKAGE bool isLifecycleMarker(const Bonus & bonus);
DLL_LINKAGE void validateTransition(const std::optional<Lifecycle> & previous, const Bonus & next, bool adding);
/// A matching lifecycle with malformed provenance or duplicate instances fails closed.
DLL_LINKAGE std::optional<Lifecycle> lifecycle(const Bonus & bonus);
DLL_LINKAGE std::optional<Lifecycle> lifecycle(const battle::Unit & unit);
DLL_LINKAGE bool priorityEligible(const battle::Unit & unit, int32_t currentRound);
DLL_LINKAGE bool blocksAdditionalActivation(const battle::Unit & unit, int32_t currentRound);
DLL_LINKAGE bool normalActivationCompleted(const battle::Unit & unit, int32_t currentRound);
/// Replace the existing local marker using ordinary SetStackEffect toUpdate.
DLL_LINKAGE std::optional<Bonus> reserveNormalActivationPlan(const battle::Unit & unit, int32_t currentRound);
DLL_LINKAGE std::optional<Bonus> completeNormalActivationPlan(const battle::Unit & unit, int32_t currentRound);
}
