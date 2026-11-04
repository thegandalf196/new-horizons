/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "BattleSide.h"
#include <cstdint>
#include <vector>
class CBattleInfoCallback;
struct HeroOrderState;
namespace heroCommands
{
DLL_LINKAGE std::vector<uint32_t> recipientIds(const CBattleInfoCallback &, BattleSide, const HeroOrderState &);
}
