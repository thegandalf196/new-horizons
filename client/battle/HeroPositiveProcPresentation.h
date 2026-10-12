/*
 * HeroPositiveProcPresentation.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "BattleConstants.h"
#include <array>

namespace heroPositiveProcPresentation
{
// Inputs are authoritative successful proc flags, not the hero's raw Luck
// or Morale values. Two successful spell perks still produce one horseshoe.
constexpr std::array<EBattleEffect, 2> effects(bool luckyDay, bool erraticMana,
	bool druidicLuck, bool heroicSpirit)
{
	return {luckyDay || erraticMana || druidicLuck ? EBattleEffect::GOOD_LUCK : EBattleEffect::INVALID,
		heroicSpirit ? EBattleEffect::GOOD_MORALE : EBattleEffect::INVALID};
}
}
