/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
class CGameHandler;
class BattleProcessor;
class CBattleInfoCallback;
namespace battleSpecialtyProcs
{
void resolveStun(CGameHandler &, BattleProcessor &, const CBattleInfoCallback &,
	uint32_t source, uint32_t target, int nominalChance);
}
