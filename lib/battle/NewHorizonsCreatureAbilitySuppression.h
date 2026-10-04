/*
 * NewHorizonsCreatureAbilitySuppression.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */

#pragma once

#include "Unit.h"

namespace newHorizonsCreatureAbilitySuppression
{
	/// 0 means unaffected, 1 is base Forgetfulness, and 2 additionally applies
	/// Mindbreaker to eligible passive offensive creature abilities.
	DLL_LINKAGE int32_t suppressionLevel(const battle::Unit & unit);

	/// True when a bonus type belongs to one of the explicitly classified
	/// Forgetfulness/Mindbreaker ability groups. Origin and native-unit identity
	/// are checked separately by filterBonuses.
	DLL_LINKAGE bool isSuppressed(const Bonus & bonus, int32_t level);

	/// Applies the unit's explicit intrinsic-ability filter to a pre-filter bonus
	/// list. When requested, stacking occurs only after suppressed entries have
	/// been removed.
	DLL_LINKAGE TConstBonusListPtr filterBonuses(const battle::Unit & unit,
		const TConstBonusListPtr & bonuses, int32_t level, bool stack);
}
