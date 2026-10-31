/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include "BattleHex.h"
#include <optional>
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace newHorizonsGnollHarriers
{
DLL_LINKAGE bool isMovementHistory(const Bonus * bonus);
DLL_LINKAGE bool trackingRequired(const CBattleInfoCallback & battle);
/// Only call with validated voluntary provenance and actual committed endpoints.
DLL_LINKAGE std::optional<Bonus> movement(const CBattleInfoCallback & battle, const battle::Unit & unit,
	bool voluntary, BattleHex before, BattleHex after);
DLL_LINKAGE int damagePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
}
