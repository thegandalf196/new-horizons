/*
 * NewHorizonsOpportunist.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace newHorizonsOpportunist
{
constexpr int MAX_MOVEMENT = 2;
DLL_LINKAGE bool hasPerk(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE int movementAllowance(const CBattleInfoCallback & battle,
	const battle::Unit * unit, int remainingMovement);
}
