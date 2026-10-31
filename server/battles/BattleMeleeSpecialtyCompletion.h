/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
class CGameHandler;
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace battleMeleeSpecialtyCompletion
{
void apply(CGameHandler & game, const CBattleInfoCallback & battle, uint32_t actorId, bool completedPrimaryMelee,
	const newHorizonsOrdinaryStrike::EffectPlan & returnedMark = newHorizonsOrdinaryStrike::EffectPlan());
}
