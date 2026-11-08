/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once

#include "../../Global.h"
#include <optional>
#include <string>
#include <string_view>

namespace battleAISelection
{
/// Existing human quick-combat condition; battles without human participants
/// are also automatically resolved. Visible auto-fight is a separate context.
DLL_LINKAGE bool automaticBattle(bool humanParticipant, bool replayAllowed,
	bool quickCombat, bool battleOnly, bool forceQuickCombat);

/// Dedicated automatic-combat setting. Older profiles without it use Quick;
/// explicit BattleAI retains unlimited search. Ordinary settings stay untouched.
DLL_LINKAGE std::string factoryName(bool automatic, std::string_view ordinary,
	std::optional<std::string_view> quick = std::nullopt);
}
