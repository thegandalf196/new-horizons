/*
 * TheChosenValue.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "HypotheticBattle.h"

class CSpell;
class DamageCache;
namespace theChosenValue
{
bool isTheChosen(const CSpell * spell);
/// Signed marginal native-ability value on existing shooting/contact exchanges.
/// No movement search or new battle branch. The active continuation is excluded.
float marginalValue(BattleSide side, uint32_t activeUnitId,
	const std::shared_ptr<CBattleInfoCallback> & before,
	const std::shared_ptr<HypotheticBattle> & after, SpellID spell,
	DamageCache & beforeCache, DamageCache & afterCache,
	float positiveMultiplier = 1.0f, float negativeMultiplier = 1.0f);
}
