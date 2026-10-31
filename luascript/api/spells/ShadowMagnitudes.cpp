/*
 * ShadowMagnitudes.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ShadowMagnitudes.h"
#include "Mechanics.h"

#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/battle/NewHorizonsSoulChain.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsShadowMagnitudes.h"

#include <algorithm>

namespace scripting::api
{
namespace
{
int32_t currentLifeDrainHealingPercent(const ::spells::Mechanics & mechanics)
{
	return newHorizonsMagic::lifeDrainHealingPercent(mechanics.getHeroCaster());
}

int32_t currentVampirismHealBasisPoints(const ::spells::Mechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	const bool nightFeeder = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL),
		std::string(newHorizonsMagic::SHADOW_NIGHT_FEEDER_PERK));
	return newHorizonsMagic::vampirismHealBasisPoints(std::max<int32_t>(0, mechanics.getEffectPower()),
		mechanics.getSpellPowerCoefficientBasisPoints(), mechanics.getWarcastingBonusPercent(),
		mechanics.getEmpowerSpellBonusPercent(), nightFeeder);
}

int32_t currentDoomCripplingPenaltyPercent(const ::spells::Mechanics & mechanics)
{
	return newHorizonsMagic::doomCripplingPenaltyPercent(mechanics.getEffectPower(),
		mechanics.getSpellPowerCoefficientBasisPoints(), mechanics.getWarcastingBonusPercent(),
		mechanics.getEmpowerSpellBonusPercent());
}
int32_t currentSoulChainEchoBasisPoints(const ::spells::Mechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	const bool soulBinder = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL), "new-horizons:shadowMagic.soulBinder");
	return newHorizonsSoulChain::echoPercentBasisPoints(std::max<int32_t>(0, mechanics.getEffectPower()),
		mechanics.getSpellPowerCoefficientBasisPoints(), soulBinder);
}

}

void registerShadowMagnitudeMethods(MethodRegistrar & registrar)
{
	registrar.function<&currentLifeDrainHealingPercent>("getLifeDrainHealingPercent", {},
		"Returns the Life Drain healing percentage for the current hero, including Blood Drinker. "
		"Actual healing uses inflicted damage and cannot exceed the recipient wound limit.");
	registrar.function<&currentVampirismHealBasisPoints>("getVampirismHealBasisPoints", {},
		"Returns this cast's Vampirism lifesteal rate in basis points using the captured coefficient "
		"and cast bonuses, with Night Feeder after the base cap. Actual healed HP remains damage-dependent.");
	registrar.function<&currentDoomCripplingPenaltyPercent>("getDoomCripplingPenaltyPercent", {},
		"Returns this cast's Doom percentage from the captured coefficient and cast bonuses. "
		"Target eligibility and conditional Initiative application are unchanged. No RNG.");
	registrar.function<&currentSoulChainEchoBasisPoints>("getSoulChainEchoBasisPoints", {},
		"Returns this cast's Soul Chain echo rate from its captured coefficient and Soul Binder. "
		"Adds no independent Warcasting/Empower multiplier and does not resolve damage or links.");
}
}
