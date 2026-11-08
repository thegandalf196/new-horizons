/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Timed.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsSpellAvailability.h"
#include "../../NewHorizonsTimedStatusPlan.h"
#include "../../NewHorizonsTimedUtility.h"
#include "../../NewHorizonsSorceryRevision.h"
#include "../../../battle/CBattleInfoCallback.h"
#include "../../../battle/IBattleState.h"
#include "../../../battle/Unit.h"
#include "../../../battle/NewHorizonsSpecialtyProcs.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include <vcmi/spells/Spell.h>

namespace spells::effects::native
{
namespace
{
JsonNode status(const std::string & type, int value, int turns, const std::string & valueType, const std::string & key)
{
	JsonNode result;
	result["type"].String() = type;
	result["val"].Integer() = value;
	result["turns"].Integer() = turns;
	result["duration"].String() = "N_TURNS";
	result["valueType"].String() = valueType;
	result["sourceType"].String() = "SPELL_EFFECT";
	result["sourceID"].String() = key;
	result["statusTags"].Vector().emplace_back("DEBUFF");
	result["statusIdentity"].String() = key;
	return result;
}

std::optional<int> specialDuration(const Mechanics * mechanics, const std::string & key)
{
	if(!mechanics->usesNewHorizonsMagicV3()) return std::nullopt;
	if(key == "core:forgetfulness") return newHorizonsMagic::forgetfulnessDurationForCast(*mechanics);
	if(key == "new-horizons:shieldOfChaos") return mechanics->adjustEffectDuration(2);
	if(key == "new-horizons:crusade") return newHorizonsMagic::timedLightEffectDuration(*mechanics);
	if(key == "new-horizons:entangle") return newHorizonsMagic::entangleDurationForCast(*mechanics);
	if(key == "new-horizons:vengefulVines") return mechanics->adjustEffectDuration(2);
	if(key == "core:misfortune") return newHorizonsMagic::misfortuneDurationForCast(*mechanics);
	return std::nullopt;
}

bool roundDuration(const JsonNode & node)
{
	return node.isString() ? node.String() == "N_TURNS"
		: node.isNumber() && node.Float() == static_cast<double>(BonusDuration::N_TURNS);
}

void convertValue(const Mechanics * mechanics, const std::string & key, const std::string & name, JsonNode & bonus)
{
	if(mechanics->usesNewHorizonsMagicV3())
	{
		if(key == "core:forgetfulness" && bonus["type"].String() == "FORGETFULL") bonus["val"].Integer() = 100;
		if(key == "core:misfortune" && name == "luck")
		{
			bonus["type"].String() = "MAXIMUM_LUCK";
			bonus["val"].Integer() = 0;
		}
		if(key == "core:slow" && name == "stacksSpeed")
			bonus["val"].Integer() = -newHorizonsSorceryRevision::slowBaseReduction(mechanics->getEffectPower(),
				mechanics->getSpellPowerCoefficientBasisPoints(), mechanics->getEmpowerSpellBonusPercent());
		if((key == "core:bless" && name == "alwaysMaximumDamage") || (key == "core:curse" && name == "alwaysMinimumDamage"))
			bonus["val"].Integer() = 0;
	}
	if(key == "core:slow" && mechanics->usesNewHorizonsMagic())
	{
		bonus["type"].String() = "STACKS_INITIATIVE";
		bonus["valueType"].String() = "ADDITIVE_VALUE";
	}
}
}

JsonMap Timed::convert(const Mechanics * mechanics) const
{
	const auto key = newHorizonsMagic::effectSpellForCast(*mechanics)->getJsonKey();
	const auto duration = specialDuration(mechanics, key);
	const int fallback = duration ? 0 : mechanics->getEffectDuration();
	JsonMap result = parameters["bonus"].Struct();
	if(key == "core:poison") newHorizonsSpecialtyProcs::scaleNativePoisonForCast(*mechanics, result);
	for(auto & [name, bonus] : result)
	{
		convertValue(mechanics, key, name, bonus);
		bonus["turns"].Integer() = duration ? *duration
			: newHorizonsMagic::configuredTimedDurationForCast(*mechanics, static_cast<int>(bonus["turns"].Float()),
				fallback, roundDuration(bonus["duration"]));
		bonus["sourceType"].String() = "SPELL_EFFECT";
		bonus["sourceID"].String() = key;
	}
	if(duration && key == "core:misfortune")
		result["favorableCreatureChanceMultiplier"] = status("FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS",
			newHorizonsMagic::misfortuneChanceForCast(*mechanics), *duration, "INDEPENDENT_MIN", key);
	if(key == "core:slow" && mechanics->battle()->getBattle()
		&& newHorizonsSorceryRevision::slowAffectsSpeed(mechanics->battle()->getBattle()->getMagicRules()))
		for(const auto & [name, bonus] : result)
			if(bonus["type"].String() == "STACKS_INITIATIVE")
			{
				auto speed = bonus;
				speed["type"].String() = "STACKS_SPEED";
				speed["valueType"].String() = "PERCENT_TO_ALL";
				result["slowSpeed"] = std::move(speed);
				break;
			}
	const auto original = mechanics->getSpell()->getJsonKey();
	for(auto & [name, bonus] : result)
	{
		if(bindPatch && original == "core:bind" && !mechanics->getHeroCaster() && bonus["type"].String() == "BIND_EFFECT")
			bonus["addInfo"].Integer() = mechanics->getUnitCaster()->unitId();
		if(shieldPatch && (original == "core:shield" || original == "core:airShield") && bonus["type"].String() == "GENERAL_DAMAGE_REDUCTION")
			bonus["val"].Float() = 100 - bonus["val"].Float();
	}
	return result;
}
}
