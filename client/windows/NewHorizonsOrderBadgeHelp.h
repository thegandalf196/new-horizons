/*
 * NewHorizonsOrderBadgeHelp.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <cstdint>
#include <algorithm>
#include <string>

#include "../../lib/battle/BattleSide.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/texts/MetaString.h"

namespace newHorizonsOrderBadgeHelp
{
inline MetaString sourceAndExpiry(const HeroOrderState & state, BattleSide side, int32_t currentRound)
{
	if(state.command == HeroCommand::NONE
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| state.issuedRound <= 0 || !state.hasScheduledRecipients(currentRound))
		return {};

	if(state.command == HeroCommand::VANGUARD && state.revision)
	{
		MetaString result;
		result.appendRawString(std::string(side == BattleSide::ATTACKER ? "Attacker" : "Defender")
			+ " Order issued in round " + std::to_string(state.issuedRound)
			+ "; expires at the end of round " + std::to_string(state.revision->expiresAfterRound)
			+ " (" + std::to_string(std::max(0, state.revision->expiresAfterRound - currentRound + 1))
			+ " rounds remaining, including this round).");
		return result;
	}

	MetaString result = MetaString::createFromTextID(currentRound == state.issuedRound
		? "new-horizons.combat.orderBadge.sourceExpiry"
		: "new-horizons.combat.orderBadge.carriedExpiry");
	result.replaceTokenTextID("%SOURCE%", side == BattleSide::ATTACKER
		? "new-horizons.combat.orderBadge.attacker"
		: "new-horizons.combat.orderBadge.defender");
	result.replaceTokenNumber("%ROUND%", state.issuedRound);
	return result;
}
}
