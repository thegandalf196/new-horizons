/*
 * NewHorizonsBattleStatus.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <string>
#include <string_view>

namespace newHorizonsBattleStatus
{
/// Time Stop currently reuses the registered SPELLINT placeholder frame. Keep
/// the status wording here so each battle stack presentation says the same
/// thing without adding a second runtime state API to the client.
inline constexpr std::string_view TIME_STOP_SPELL_KEY = "new-horizons:timeStop";

inline bool isTimeStop(std::string_view spellKey)
{
	return spellKey == TIME_STOP_SPELL_KEY;
}

inline std::string timeStopTooltip(std::string_view spellDescription)
{
	std::string result = "TIME STOP - STASIS\n";
	result += spellDescription;
	result += "\n\nRemaining: until the beginning of the caster's next Hero Action.";
	return result;
}

/// A short badge fits inside the 48x36 SPELLINT slot while the hover text
/// carries the full remaining-until-caster-action semantics.
inline constexpr std::string_view TIME_STOP_BADGE = "ST";
}
