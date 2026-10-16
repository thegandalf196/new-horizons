/*
 * UnitAdjacency.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

class BattleHexArray;
namespace battle
{
class Unit;
/// Exactly the live battleAdjacentUnits predicate. The roster owner supplies
/// candidate membership/order; no extra side, ghost or target-validity filter.
DLL_LINKAGE bool isAdjacentLivingUnit(const BattleHexArray & surroundingHexes, const Unit & candidate);
}
