/*
 * HeroCommandCoefficientQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleProxy.h"
#include "IBattleState.h"
#include "NewHorizonsDivineMandate.h"

int CBattleInfoCallback::heroCommandCoefficient(const JsonNode & effect, const CGHeroInstance & hero,
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent) const
{
	if(const auto * world = dynamic_cast<const CBattleInfoCallback *>(getBattle()); world && world != this)
		return forwardHeroCommandCoefficient(*world, effect, hero, warcastingBonusPercent, divineMandateEfficiencyBonusPercent);
	return heroCommands::coefficient(effect, hero, warcastingBonusPercent, divineMandateEfficiencyBonusPercent);
}

int CBattleInfoCallback::forwardHeroCommandCoefficient(const CBattleInfoCallback & source, const JsonNode & effect,
	const CGHeroInstance & hero, int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent)
{
	return source.heroCommandCoefficient(effect, hero, warcastingBonusPercent, divineMandateEfficiencyBonusPercent);
}

int BattleProxy::heroCommandCoefficient(const JsonNode & effect, const CGHeroInstance & hero,
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent) const
{
	return forwardHeroCommandCoefficient(*subject, effect, hero, warcastingBonusPercent, divineMandateEfficiencyBonusPercent);
}

int CBattleInfoCallback::heroOrderCoefficient(const JsonNode & formula, const CGHeroInstance * hero,
	const HeroOrderState * orderState, uint32_t recipient, bool scaleLucky) const
{
	const int intrinsic = hero ? heroCommandCoefficient(formula, *hero,
		orderState ? orderState->warcastingBonusPercent : 0,
		orderState ? orderState->divineMandateEfficiencyBonusPercent() : 0) : 0;
	const int value = hero ? heroCommandCoefficient(newHorizonsDivineMandate::crownOrderFormula(formula,
		orderState && orderState->crownAndAltarAppliesTo(recipient)), *hero,
		orderState ? orderState->warcastingBonusPercent : 0,
		orderState ? orderState->divineMandateEfficiencyBonusPercent() : 0) : 0;
	return scaleLucky && orderState
		? heroCommands::orderPotency(intrinsic, value, *orderState, getBattle()->getMagicRules()) : value;
}
