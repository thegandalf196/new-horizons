/*
 * PotentialTargets.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "AttackPossibility.h"

class PotentialTargets
{
public:
	std::vector<AttackPossibility> possibleAttacks;
	battle::Units unreachableEnemies;
	std::vector<ForcedAction> forcedBerserkActions;
	bool berserk = false;

	PotentialTargets(){};
	PotentialTargets(
		const battle::Unit * attacker,
		DamageCache & damageCache,
		std::shared_ptr<HypotheticBattle> hb);

	const AttackPossibility & bestAction() const;
	int64_t bestActionValue() const;
	/// Expected immediate action value across the deterministic tied Berserk candidates,
	/// measured from the berserker's side in AttackPossibility damage-reduction units.
	/// WALK and NO_ACTION contribute zero.
	float expectedBerserkActionValue() const;
};
