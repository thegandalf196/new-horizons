/*
 * BattleAttackInfo.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleAttackInfo.h"
#include "CUnitState.h"

BattleAttackInfo::BattleAttackInfo(const battle::Unit * Attacker, const battle::Unit * Defender, int chargeDistance, bool Shooting)
	: attacker(Attacker),
	defender(Defender),
	shooting(Shooting),
	attackerPos(BattleHex::INVALID),
	defenderPos(BattleHex::INVALID),
	chargeDistance(chargeDistance)
{
	// Spell-like creature shots (Liches, Magogs, and modded equivalents) are
	// magical attacks even when AI and preview callers construct the attack
	// directly rather than going through the authoritative packet path.
	physicalDamage = !(shooting && attacker && attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
}

BattleAttackInfo BattleAttackInfo::reverse() const
{
	BattleAttackInfo ret(defender, attacker, 0, false);

	ret.defenderPos = attackerPos;
	ret.attackerPos = defenderPos;
	ret.retaliation = true;
	return ret;
}
