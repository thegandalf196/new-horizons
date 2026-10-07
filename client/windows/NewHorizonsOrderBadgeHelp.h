/*
 * NewHorizonsOrderBadgeHelp.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <cstdint>

#include "../../lib/battle/BattleSide.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/texts/MetaString.h"

namespace newHorizonsOrderBadgeHelp
{
inline MetaString sourceAndExpiry(const HeroOrderState & state, BattleSide side, int32_t currentRound)
{
	if(state.command == HeroCommand::NONE
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| state.issuedRound <= 0 || currentRound != state.issuedRound)
		return {};

	MetaString result = MetaString::createFromTextID("new-horizons.combat.orderBadge.sourceExpiry");
	result.replaceTokenTextID("%SOURCE%", side == BattleSide::ATTACKER
		? "new-horizons.combat.orderBadge.attacker"
		: "new-horizons.combat.orderBadge.defender");
	result.replaceTokenNumber("%ROUND%", state.issuedRound);
	return result;
}
}
