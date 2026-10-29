/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../bonuses/Bonus.h"
#include <optional>
namespace battle { class Unit; }
class CBattleInfoCallback;
class IBattleInfo;
namespace newHorizonsParalyzingGaze
{
/// Recognition and structural validation never require the identifier library.
DLL_LINKAGE bool isMarker(const Bonus & bonus) noexcept;
DLL_LINKAGE bool validateMarker(const Bonus & bonus);
DLL_LINKAGE Bonus marker();
DLL_LINKAGE bool captured(const IBattleInfo & battle);
DLL_LINKAGE void validateApplication(const IBattleInfo & battle, uint32_t target, const Bonus & bonus);
DLL_LINKAGE void validateLoadedMarker(const Bonus & bonus);
/// Single static token: reapplication is idempotent, not another cancellation.
DLL_LINKAGE std::optional<Bonus> pending(const battle::Unit & target);
DLL_LINKAGE bool qualifiesAttack(const CBattleInfoCallback & battle, const battle::Unit * source,
    const battle::Unit * primary, bool ranged, bool physical, bool secondary);
/// Called only AFTER the existing ordinary-retaliation eligibility gates.
/// A pending token alone never denies resource capacity in ableToRetaliate().
DLL_LINKAGE bool canConsume(const battle::Unit & retaliator);
}
