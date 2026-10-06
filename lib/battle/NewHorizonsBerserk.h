/*
 * NewHorizonsBerserk.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <optional>
#include <vector>

#include "../bonuses/Bonus.h"

class CBattleInfoCallback;

namespace battle
{
class Unit;
}

namespace newHorizonsBerserk
{
/// Returns the temporary Frenzied Curse Speed bonus for a valid, active NH Berserk effect.
/// The original Berserk caster's saved active perk is used, not the recipient's current owner.
DLL_LINKAGE std::optional<Bonus> forcedActivationSpeedBonus(const CBattleInfoCallback & battle,
	const battle::Unit * unit);

/// Returns copies of the saved-v3 Berserk spell control bonuses to remove after its accepted forced action.
DLL_LINKAGE std::vector<Bonus> completedForcedActivationBonuses(const CBattleInfoCallback & battle,
	const battle::Unit * unit);

/// Matches only the generated action-scoped Frenzied Curse Speed bonus.
DLL_LINKAGE bool isFrenziedCurseSpeedBonus(const Bonus * bonus);
}
