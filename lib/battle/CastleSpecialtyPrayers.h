/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "BattleSide.h"
#include "../bonuses/Bonus.h"
#include <optional>
#include <vector>

class BattleAction;
class CBattleInfoCallback;
class CGHeroInstance;

/// Castle specialties that grant Consecrated Arms prayers outside an Order's own
/// recipient choice: Loynis's Sermon and Adela's Benediction of Arms (captured
/// revisedSpecialties, format 1166). Both need Chosen Prayers (version 4).
namespace castleSpecialtyPrayers
{
inline constexpr const char * BENEDICTION_STACKING = "new-horizons:benedictionOfArms.pending";
/// Loynis casting a Light spell as his hero action: it must carry one chosen prayer.
DLL_LINKAGE bool sermonCast(const CBattleInfoCallback & battle, const BattleAction & action);
/// Friendly stacks a Sermon prayer may reach, sorted.
DLL_LINKAGE std::vector<uint32_t> sermonRecipients(const CBattleInfoCallback & battle, BattleSide side);
/// Marks the stack Adela has just Blessed for her next Order's prayer.
DLL_LINKAGE std::optional<Bonus> benedictionMarker(const CGHeroInstance * hero);
DLL_LINKAGE bool isBenedictionMarker(const Bonus * bonus);
/// The living friendly prayer holder carrying Adela's pending Benediction.
DLL_LINKAGE std::optional<uint32_t> benedictionRecipient(const CBattleInfoCallback & battle, BattleSide side);
/// Every unit of `side` carrying a Benediction marker, for consumption by the next Order.
DLL_LINKAGE std::vector<uint32_t> benedictionHolders(const CBattleInfoCallback & battle, BattleSide side);
}
