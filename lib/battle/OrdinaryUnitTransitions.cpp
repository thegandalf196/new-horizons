/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "OrdinaryUnitTransitions.h"
namespace battle
{
void resetOrdinaryRoundFlags(bool & defending, bool & waiting,
	bool & waitedThisTurn, bool & movedThisRound, bool & hadMorale,
	bool & waitBonusUsed, bool & waitMasteryDoubled) noexcept
{
	defending = false;
	waiting = false;
	waitedThisTurn = false;
	movedThisRound = false;
	hadMorale = false;
	waitBonusUsed = false;
	waitMasteryDoubled = false;
}
void finishOrdinaryAttackFlags(bool waitedThisTurn, bool physical,
	bool & battlecraftWaitBonusUsed, bool & battlecraftWaitMasteryDoubled) noexcept
{
	if(physical && waitedThisTurn)
	{
		battlecraftWaitBonusUsed = true;
		battlecraftWaitMasteryDoubled = false;
	}
}
std::optional<int32_t> consumeCounterValue(int32_t available, bool limited, int32_t amount) noexcept
{
	if(amount < 0 || (limited && (available < 0 || amount > available))) return {};
	return limited ? available - amount : available;
}
}
