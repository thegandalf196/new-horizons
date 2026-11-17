/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleRecallAnchors.h"
#include "BattleInfo.h"
#include "../CStack.h"

void captureBattleRecallAnchors(BattleInfo & battle)
{
	for(const auto & stack : battle.stacks)
		if(stack && !stack->isGhost() && stack->getPosition().isAvailable())
			stack->initialPosition = stack->getPosition();
}
