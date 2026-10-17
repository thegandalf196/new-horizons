/*
 * BattleBonusAcceptance.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

class CStack;
class CGHeroInstance;
struct Bonus;
namespace battle
{
class CUnitState;
/// Original supplies the unchanged hero/perk identity; state supplies form and
/// summon provenance. Does not collect or mutate either bonus graph.
DLL_LINKAGE bool acceptsBattleBonus(const CStack & original, const CUnitState & state, const Bonus & bonus);
DLL_LINKAGE bool acceptsBattleBonus(const CStack & original, const CUnitState & state, const Bonus & bonus,
	const CGHeroInstance * hero);
}
