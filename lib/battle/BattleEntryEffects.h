/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
class BattleInfo;
namespace battle
{
/// Fresh setup only; descriptor localInit must not reapply entry effects.
DLL_LINKAGE void initializeEntryEffects(BattleInfo & battle, int32_t day, int32_t week);
}
