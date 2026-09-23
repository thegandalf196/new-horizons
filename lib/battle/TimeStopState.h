/*
 * TimeStopState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "BattleSide.h"
#include "../bonuses/Bonus.h"

namespace timeStopState
{
/// Recognizes both canonical Time Stop effects and legacy/content-light markers.
DLL_LINKAGE bool isTimeStopBonus(const Bonus & bonus);

/// True for the stack-state markers removed when a caster's Hero Action expires Time Stop.
DLL_LINKAGE bool isStateBonus(const Bonus & bonus);

/// Legacy markers without caster parameters are cleaned up on either Hero Action.
DLL_LINKAGE bool belongsToSide(const Bonus & bonus, BattleSide side);
}
