/*
 * NewHorizonsConfusionControl.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../bonuses/Bonus.h"

namespace newHorizonsConfusionControl
{
DLL_LINKAGE Bonus pendingMarker(SpellID spell, PlayerColor caster, bool confounder);
DLL_LINKAGE void validateMarker(const Bonus & marker);
DLL_LINKAGE bool isPendingMarker(const Bonus * marker);
}
