/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/constants/NumericConstants.h"
void HypotheticBattle::captureSideCombatState(const CBattleInfoCallback & source, BattleSide side)
{
	focusFireStates[side] = source.battleGetFocusFireState(side);
	fortuneStates[side] = source.getBattle()->getSylvanLuckState(side);
	perfectFortuneStates[side] = source.getBattle()->getPerfectFortuneState(side);
	luckSerendipityStates[side] = source.getBattle()->getLuckSerendipityState(side);
	adverseRerollStates[side] = source.getBattle()->getAdverseCombatRerollState(side);
	moraleSuppressionStates[side] = source.getBattle()->getMoraleSuppressionState(side);
	bloodrageRanks[side] = source.getBattle()->getBloodrageRank(side);
	bloodrageDamagePercents[side] = source.getBattle()->getBloodrageDamagePercent(side);
	bloodrageCaps[side] = source.getBattle()->getBloodrageCapPercent(side);
	bloodrageSpeedBonuses[side] = source.getBattle()->getBloodrageSpeedBonus(side);
	bloodrageAdditionalRetaliations[side] = source.getBattle()->getBloodrageAdditionalRetaliations(side);
	bloodrageLowHealthIncrements[side] = source.getBattle()->getBloodrageLowHealthIncrement(side);
	bloodragePainIncrements[side] = source.getBattle()->getBloodragePainIncrement(side);
}
