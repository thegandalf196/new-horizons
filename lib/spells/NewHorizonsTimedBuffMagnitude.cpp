/*
 * NewHorizonsTimedBuffMagnitude.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsTimedBuffMagnitude.h"
#include "CSpell.h"
#include "../bonuses/BonusCustomTypes.h"
#include "../bonuses/BonusParameters.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
std::optional<double> configuredAdditiveTimedValue(const CSpell & spell, int32_t effectLevel, BonusType type)
{
	const auto & level = spell.getLevelInfo(std::clamp(effectLevel, 0, 3));
	std::optional<double> result;
	const auto take = [&](const JsonNode & definition, int32_t fallback)
	{
		if(static_cast<BonusType>(BonusTypeID::decode(definition["type"].String())) != type)
			return true;
		const auto & mode = definition["valueType"];
		if(!mode.isNull() && (!mode.isString() || !bonusValueMap.contains(mode.String())))
			return false;
		const auto valueType = mode.isNull() ? BonusValueType::ADDITIVE_VALUE : bonusValueMap.at(mode.String());
		if(result || (valueType != BonusValueType::ADDITIVE_VALUE && valueType != BonusValueType::BASE_NUMBER))
			return false;
		result = definition["val"].isNull() ? fallback : definition["val"].Float();
		return true;
	};
	if(level.battleEffects.isNull())
	{
		// Match FallbackMechanicsFactory: explicit val wins, absent val uses power.
		const auto & effects = level.effects.Struct().empty() ? level.cumulativeEffects : level.effects;
		for(const auto & [name, definition] : effects.Struct())
			if(!take(definition, level.power))
				return std::nullopt;
	}
	else
		for(const auto & [name, effect] : level.battleEffects.Struct())
			if(effect["type"].String() == "timed" || effect["type"].String() == "core:timed")
				for(const auto & [bonusName, definition] : effect["bonus"].Struct())
					if(!take(definition, 0))
						return std::nullopt;
	return result;
}

std::optional<double> casterEnchantValueOverride(const CGHeroInstance * hero, SpellID spell, double value)
{
	if(!hero)
		return std::nullopt;
	const auto add = hero->getBonusesOfType(BonusType::SPECIAL_ADD_VALUE_ENCHANT, BonusSubtypeID(spell));
	const auto fixed = hero->getBonusesOfType(BonusType::SPECIAL_FIXED_VALUE_ENCHANT, BonusSubtypeID(spell));
	if(add->empty() && fixed->empty())
		return std::nullopt;
	if(!add->empty())
		value += add->front()->parameters ? add->front()->parameters->toNumber() : 0;
	if(!fixed->empty())
		value = fixed->front()->parameters ? fixed->front()->parameters->toNumber() : 0;
	return value;
}

double casterEnchantValue(const CGHeroInstance * hero, SpellID spell, double value)
{
	return casterEnchantValueOverride(hero, spell, value).value_or(value);
}
}
