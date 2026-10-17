/*
 * OrdinaryMeleeRetaliation.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

namespace battle
{
class Unit;

/// Captured by the existing ordinary-melee caller at its original boundaries.
/// Defiant denials are resolved BEFORE supplying counterBlocked/shroudBlocked.
/// Does not admit an attack, move units, spend retaliation or resolve First Strike.
struct OrdinaryMeleeRetaliationContext
{
	bool counterBlocked;
	bool longWeaponAttack;
	bool longReachAttack;
	int attackIndex;
	bool firstStrike;
	bool shroudBlocked;
	bool frozenBeforeHit;
};

/// Preserve native short-circuit order; do NOT eagerly query the units for the
/// three unit-owned predicates. Input identities/lifetimes are caller-owned.
DLL_LINKAGE bool canRetaliateAfterOrdinaryMelee(const Unit & attacker, const Unit & defender,
	const OrdinaryMeleeRetaliationContext & context);
}
