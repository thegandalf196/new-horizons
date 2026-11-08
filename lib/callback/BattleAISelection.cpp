/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleAISelection.h"
#include <stdexcept>

namespace battleAISelection
{
bool automaticBattle(bool humanParticipant, bool replayAllowed,
	bool quickCombat, bool battleOnly, bool forceQuickCombat)
{
	return !humanParticipant || (replayAllowed && (quickCombat || battleOnly)) || forceQuickCombat;
}

std::string factoryName(bool automatic, std::string_view ordinary,
	std::optional<std::string_view> quick)
{
	if(!automatic)
		return std::string(ordinary);
	if(!quick)
		return "BattleAIQuick";
	if(*quick != "BattleAIQuick" && *quick != "BattleAI")
		throw std::invalid_argument("Automatic combat AI must be BattleAIQuick or BattleAI");
	return std::string(*quick);
}
}
