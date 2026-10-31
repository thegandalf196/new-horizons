/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
class CGameHandler;
class CBattleInfoCallback;
namespace battleMeleeSpecialtyCompletion
{
void apply(CGameHandler & game, const CBattleInfoCallback & battle, uint32_t actorId, bool completedPrimaryMelee);
}
