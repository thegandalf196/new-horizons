/*
 * HeroPositiveProcPresentation.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "BattleConstants.h"
#include <array>
#include <cstdint>

namespace heroPositiveProcPresentation
{
// Diagnostic-only value context: no owner pointer, RNG data or lifetime action.
struct TraceContext
{
	long long scene = 0;
	uint64_t ordinal = 0;
	int battle = -1;
	int side = -1;
	int round = -1;
	bool spell = false;
	int source = -1;
	unsigned flags = 0;
	int effect = -1;
	bool firstDrawReturned = false;
	bool markFirstDrawReturned()
	{
		if(firstDrawReturned)
			return false;
		firstDrawReturned = true;
		return true;
	}
};

// Same literal opt-in/strict limit parser; separate hero-only child-domain budget.
bool traceEnabled();
void trace(TraceContext & context, const char * event, int animation = -1,
	int frame = -1, int frames = 0, int stage = -1, int x = 0, int y = 0,
	bool imageAvailable = false);

// Inputs are authoritative successful proc flags, not the hero's raw Luck
// or Morale values. Two successful spell perks still produce one horseshoe.
constexpr std::array<EBattleEffect, 2> effects(bool luckyDay, bool erraticMana,
	bool druidicLuck, bool heroicSpirit)
{
	return {luckyDay || erraticMana || druidicLuck ? EBattleEffect::GOOD_LUCK : EBattleEffect::INVALID,
		heroicSpirit ? EBattleEffect::GOOD_MORALE : EBattleEffect::INVALID};
}
}
