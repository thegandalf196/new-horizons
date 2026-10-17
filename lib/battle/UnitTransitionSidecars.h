/*
 * UnitTransitionSidecars.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include <cstdint>
struct Bonus;
struct ReducedExtraActivationState;
namespace battle
{
class Unit;
/// Existing first-match Protect endpoints; includes alive/footprint checks.
DLL_LINKAGE bool protectOrderIsSeparated(const Unit * protector, const Unit * ward);
/// Existing scalar UPDATE/REMOVE completion rule, never resets the allowance.
DLL_LINKAGE void clearReducedActivationForUnit(ReducedExtraActivationState & state, uint32_t id);
/// Original persistent-spell exception, shared by live and projected cleanup.
DLL_LINKAGE bool isTransientSpellEffect(const Bonus * bonus);
}
