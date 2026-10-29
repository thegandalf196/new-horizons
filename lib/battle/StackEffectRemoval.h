/*
 * StackEffectRemoval.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
struct Bonus;
namespace battle
{
/// Legacy removal fields, with typed source-bound restraints kept independent.
DLL_LINKAGE bool matchesStackEffectRemoval(const Bonus & selected, const Bonus & candidate);
}
