/*
 * OrdinaryMeleeRetaliation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OrdinaryMeleeRetaliation.h"
#include "Unit.h"

namespace battle
{
bool canRetaliateAfterOrdinaryMelee(const Unit & attacker, const Unit & defender,
	const OrdinaryMeleeRetaliationContext & context)
{
	return attacker.alive()
		&& !context.counterBlocked
		&& !attacker.isInvincible()
		&& !context.longWeaponAttack
		&& !context.longReachAttack
		&& (context.attackIndex == 0 && !context.firstStrike)
		&& !context.shroudBlocked
		&& !context.frozenBeforeHit
		&& defender.ableToRetaliate();
}
}
