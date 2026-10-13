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
	bool luckReceiptPresent = false;
	bool moraleReceiptPresent = false;
	int luckHeroValue = -1;
	int luckBaseThreshold = -1;
	int luckDiceSize = -1;
	int moraleHeroValue = -1;
	int moraleBaseThreshold = -1;
	int moraleDiceSize = -1;
	uint32_t moraleGrantId = 0;
	int luckyDayPolicyScale = -1;
	int erraticManaPolicyScale = -1;
	int druidicLuckPolicyScale = -1;
	int heroicSpiritPolicyScale = -1;
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

// Copies only the three public receipt summary values; never draw/bias/seed.
// Shared by actual opt-in assembly and the dependency-free component test.
template<typename Receipt>
void captureReceiptEvidence(TraceContext & context, const Receipt * luck,
	const Receipt * morale, uint32_t moraleGrantId)
{
	context.luckReceiptPresent = luck != nullptr;
	context.moraleReceiptPresent = morale != nullptr;
	context.luckHeroValue = luck ? luck->heroValue : -1;
	context.luckBaseThreshold = luck ? luck->baseThreshold : -1;
	context.luckDiceSize = luck ? luck->diceSize : -1;
	context.moraleHeroValue = morale ? morale->heroValue : -1;
	context.moraleBaseThreshold = morale ? morale->baseThreshold : -1;
	context.moraleDiceSize = morale ? morale->diceSize : -1;
	context.moraleGrantId = moraleGrantId;
}

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
