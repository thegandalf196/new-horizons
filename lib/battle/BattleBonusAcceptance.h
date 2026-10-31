/*
 * BattleBonusAcceptance.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "../constants/EntityIdentifiers.h"

class CStack;
class CGHeroInstance;
struct Bonus;
namespace battle
{
class CUnitState;
/// Copied inputs only; no native Hero/Unit/Battle/library references.
struct BonusAcceptanceFacts
{
	struct Static
	{
		SecondarySkill sylvanSkill;
		bool tacticsEnabled = false;
		bool heroPresent = false;
		bool wildChance = false;
		bool turret = false;
		SlotID slot;
	};
	struct State
	{
		bool hasForm = false;
		CreatureID currentForm;
		CreatureID originalForm;
		bool ghost = false;
		bool summoned = false;
		bool natureSummoned = false;
	};
	Static metadata;
	State state;
};
DLL_LINKAGE bool acceptsBattleBonus(const Bonus & bonus,
	const BonusAcceptanceFacts::Static & metadata, const BonusAcceptanceFacts::State & state);
/// Original supplies the unchanged hero/perk identity; state supplies form and
/// summon provenance. Does not collect or mutate either bonus graph.
DLL_LINKAGE bool acceptsBattleBonus(const CStack & original, const CUnitState & state, const Bonus & bonus);
DLL_LINKAGE bool acceptsBattleBonus(const CStack & original, const CUnitState & state, const Bonus & bonus,
	const CGHeroInstance * hero);
}
