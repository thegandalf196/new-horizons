/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#pragma once

#include "../../lib/constants/EntityIdentifiers.h"
class CCallback;

namespace NK2AI
{
/// Passive administration only: one legal unassigned qualified hero per town.
void appointAvailableStewards(CCallback & callback, PlayerColor owner);
}
