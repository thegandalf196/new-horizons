/*
 * AttackPreviewArcheryAdmission.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "AttackPreviewArcheryAdmission.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include <memory>

namespace attackPreview
{
ArcheryReceiptAdmission archeryReceiptAdmission(const BattleAttackInfo & attack,
	bool ordinaryShooter, const CGHeroInstance * hero, int32_t round, int32_t activationSerial)
{
	std::shared_ptr<battle::CUnitState> initialState;
	const auto snapshot = [&]() -> const battle::CUnitState *
	{
		// Share one full detached snapshot; no surrogate receipt or cross-call cache.
		if(!initialState)
			initialState = attack.attacker->acquireState();
		return initialState.get();
	};
	ArcheryReceiptAdmission result;
	result.deadeye = attack.shooting && attack.physicalDamage && !attack.retaliation
		&& ordinaryShooter && hero && newHorizonsArchery::hasDeadeye(hero)
		&& snapshot()->archeryDeadeyeRound != round;
	result.crossfire = ordinaryShooter && hero && newHorizonsArchery::hasCrossfire(hero);
	result.suppression = attack.shooting && attack.physicalDamage && !attack.retaliation
		&& ordinaryShooter && hero && newHorizonsArchery::hasSuppression(hero)
		&& snapshot()->archerySuppressionActivationSerial != activationSerial;
	result.rainOfArrows = attack.shooting && attack.physicalDamage && !attack.retaliation
		&& ordinaryShooter && hero && newHorizonsArchery::hasRainOfArrows(hero)
		&& snapshot()->archeryRainOfArrowsActivationSerial != activationSerial;
	return result;
}
}
