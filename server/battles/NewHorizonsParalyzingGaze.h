/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/battle/NewHorizonsParalyzingGaze.h"
class CGameHandler;
class CStack;
namespace newHorizonsAerialSupremacy { struct Excursion; }
namespace newHorizonsParalyzingGaze
{
/// Called after canonical eligibility, before any counter attack pipeline.
/// true means this ordinary opportunity was canceled; false permits the hit.
bool consumeRetaliation(CGameHandler & handler, const CBattleInfoCallback & battle,
	const CStack & retaliator, const CStack & opponent, bool ranged,
	const newHorizonsAerialSupremacy::Excursion * aerial = nullptr);
void applyAfterShot(CGameHandler & handler, const CBattleInfoCallback & battle,
	uint32_t sourceId, uint32_t targetId, bool admitted);
}
