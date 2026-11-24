/*
 * Part of VCMI / New Horizons.
 * Authors: listed in AUTHORS. GPL-2.0-or-later; see license.txt.
 */
#pragma once

#include "../../lib/pathfinder/NewHorizonsMarchLeft.h"

#include <functional>
#include <string>
#include <vector>

/// Warns before an exchange that would lower a hero's movement (Day's March).
/// The server applies the limit; this only previews it.
namespace marchLeftWarning
{
using Transfers = std::vector<newHorizonsMarchLeft::StackTransfer>;

/// One stack (or part of it) moving into another army, merging with a same-creature stack.
Transfers move(const CArmedInstance * from, SlotID fromSlot, const CArmedInstance * to, SlotID toSlot);
/// Two stacks trading places between two armies.
Transfers swap(const CArmedInstance * first, SlotID firstSlot, const CArmedInstance * second, SlotID secondSlot);
/// The stack goes to the slot when it is empty or holds the same creature, else they trade places.
Transfers mergeOrSwap(const CArmedInstance * from, SlotID fromSlot, const CArmedInstance * to, SlotID toSlot);
/// Every stack of one army moving into the other.
Transfers wholeArmy(const CArmedInstance * from, const CArmedInstance * to);

/// "These troops have 25% of today's march left. Gem will have 450 movement left (was 1800)." per
/// affected hero; empty when no hero's movement changes.
std::string describe(const Transfers & transfers);

/// Sends at once when no movement changes; otherwise shows the warning with OK / Cancel.
void confirm(const Transfers & transfers, const std::function<void()> & send);
}
