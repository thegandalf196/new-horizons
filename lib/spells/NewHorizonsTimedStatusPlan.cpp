/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsTimedStatusPlan.h"
#include "NewHorizonsTimedUtility.h"
#include "NewHorizonsTimedMagnitudes.h"
#include "ISpellMechanics.h"
#include "NewHorizonsTimedBuffMagnitude.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "../battle/Unit.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/StackEffectRefresh.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include <cmath>
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <limits>
namespace newHorizonsMagic
{
// This deliberately does not use parseBonus: that loader can register deferred
// identifier callbacks. Unsupported custom producers receive no exact preview.
std::optional<Bonus> configuredRoundBonusIdentity(const CSpell & spell, int level, BonusType type)
{
	std::optional<Bonus> result;
	bool supported = true;
	const auto take = [&](const JsonNode & definition)
	{
		if(static_cast<BonusType>(BonusTypeID::decode(definition["type"].String())) != type)
			return;
		if(result || definition["duration"].String() != "N_TURNS"
			|| !definition["updater"].isNull() || !definition["propagationUpdater"].isNull()
			|| !definition["limiters"].isNull() || !definition["propagator"].isNull())
		{
			supported = false;
			return;
		}
		Bonus bonus;
		bonus.type = type;
		bonus.duration = BonusDuration::N_TURNS;
		const auto & mode = definition["valueType"];
		if(!mode.isNull())
		{
			if(!mode.isString() || !bonusValueMap.contains(mode.String()))
			{
				supported = false;
				return;
			}
			bonus.valType = bonusValueMap.at(mode.String());
		}
		const auto & subtype = definition["subtype"];
		if(subtype.isString())
			bonus.subtype = decodeBonusSubtype(type, subtype.String());
		else if(!subtype.isNull())
		{
			supported = false;
			return;
		}
		bonus.statusIdentity = definition["statusIdentity"].String();
		const auto value = definition["val"].Float();
		const auto turns = definition["turns"].Float();
		if(!std::isfinite(value) || value < std::numeric_limits<int32_t>::min()
			|| value > std::numeric_limits<int32_t>::max() || !std::isfinite(turns)
			|| turns < std::numeric_limits<int16_t>::min() || turns > std::numeric_limits<int16_t>::max())
		{
			supported = false;
			return;
		}
		bonus.val = static_cast<int32_t>(value);
		bonus.turnsRemain = static_cast<int16_t>(turns);
		result = std::move(bonus);
	};
	const auto & info = spell.getLevelInfo(std::clamp(level, 0, 3));
	if(info.battleEffects.isNull())
	{
		const auto & effects = info.effects.Struct().empty() ? info.cumulativeEffects : info.effects;
		for(const auto & [name, definition] : effects.Struct())
			take(definition);
	}
	else
		for(const auto & [name, effect] : info.battleEffects.Struct())
			if(effect["type"].String() == "timed" || effect["type"].String() == "core:timed")
				for(const auto & [bonusName, definition] : effect["bonus"].Struct())
					take(definition);
	return supported ? result : std::nullopt;
}

int32_t refreshedTimedStatusDuration(const spells::Mechanics & mechanics, const battle::Unit & recipient,
	Bonus incoming, int32_t rounds)
{
	incoming.source = BonusSource::SPELL_EFFECT;
	incoming.sid = BonusSourceID(effectSpellForCast(mechanics)->getId());
	incoming.turnsRemain = rounds;
	for(const auto & existing : mechanics.battle()->getBattle()->getLocalSpellEffects(recipient.unitId()))
		if(stackEffectRefresh::matchesIdentity(incoming, &existing))
			stackEffectRefresh::refreshFields(incoming, existing);
	return incoming.turnsRemain;
}

int64_t holyArmorReductionForRecipient(const spells::Mechanics & m, int32_t configuredBase,
	const battle::Unit & unit)
{
	const auto * hero = m.getHeroCaster();
	const bool aegis = hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.aegis");
	return newHorizonsMagic::holyArmorReduction(configuredBase, m.getEffectPower(),
		m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent(),
		aegis, m.getCrownAndAltarBonusPercent(&unit));
}
int32_t forgetfulnessDurationForCast(const spells::Mechanics & m)
{
	return m.adjustEffectDuration(newHorizonsMagic::forgetfulnessBaseDuration(m.getEffectPower(),
		m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent()));
}
int32_t entangleDurationForCast(const spells::Mechanics & m)
{
	const auto * hero = m.getHeroCaster();
	return m.adjustEffectDuration(newHorizonsMagic::entangleBaseDuration(m.getEffectPower(),
		m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent(),
		hero && hero->hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.rootcaller")));
}
int32_t misfortuneDurationForCast(const spells::Mechanics & m)
{
	return m.adjustEffectDuration(newHorizonsMagic::misfortuneBaseDuration(m.getEffectPower(),
		m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent()));
}
int32_t misfortuneChanceForCast(const spells::Mechanics & m)
{
	const auto * hero = m.getHeroCaster();
	return newHorizonsMagic::misfortuneChanceMultiplier(m.getEffectPower(), m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent(),
		hero && hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.misfortuneWeaver"));
}
int32_t chaosShieldReductionForCast(const spells::Mechanics & m)
{
	const auto * hero = m.getHeroCaster();
	return newHorizonsMagic::shieldOfChaosReduction(m.getEffectPower(), m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent(),
		hero && hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.paradoxShield"));
}
int64_t heavenlyGaleReductionForRecipient(const spells::Mechanics & m, const battle::Unit & unit)
{
	const auto * hero = m.getHeroCaster();
	return newHorizonsMagic::heavenlyGaleReductionBasisPoints(m.getEffectPower(),
		m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent(),
		hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.aegis"),
		m.getCrownAndAltarBonusPercent(&unit));
}
int32_t configuredTimedDuration(int32_t configuredTurns, int32_t fallbackTurns,
	bool roundTimer, int32_t extendRounds)
{
	if(configuredTurns == 0)
		return fallbackTurns;
	if(!roundTimer)
		return configuredTurns;
	return static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(configuredTurns)
		+ extendRounds, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}
int32_t configuredTimedDurationForCast(const spells::Mechanics & mechanics, int32_t configuredTurns,
	int32_t fallbackTurns, bool roundTimer)
{
	return configuredTimedDuration(configuredTurns, fallbackTurns, roundTimer, mechanics.getExtendSpellBonusRounds());
}
double misfortuneValueForRecipient(const spells::Mechanics & mechanics, const battle::Unit & unit, double value)
{
	const auto * hero = mechanics.getHeroCaster();
	if(!hero)
		return value;
	const auto * family = effectSpellForCast(mechanics);
	const auto spellId = family ? family->getId() : mechanics.getSpellId();
	const int tier = std::max(1, unit.creatureLevel());
	const auto scaling = hero->getBonusesOfType(BonusType::SPECIAL_SPELL_SCALING, BonusSubtypeID(spellId));
	if(!scaling->empty())
	{
		const int64_t percent = static_cast<int64_t>(scaling->front()->val) * (hero->level / tier);
		if(percent != 0)
			value = std::floor(value * (100 + percent) / 100);
	}
	const auto peculiar = hero->getBonusesOfType(BonusType::SPECIAL_PECULIAR_ENCHANT, BonusSubtypeID(spellId));
	if(!peculiar->empty())
	{
		const auto & bonus = peculiar->front();
		if(bonus->parameters && bonus->parameters->isVector() && !bonus->parameters->toVector().empty())
		{
			const auto & values = bonus->parameters->toVector();
			value += values[std::min<size_t>(tier - 1, values.size() - 1)];
		}
		else
		{
			const int power = tier <= 2 ? 3 : (tier <= 4 ? 2 : (tier <= 6 ? 1 : 0));
			value += mechanics.isNegativeSpell() ? -power : power;
		}
	}
	return casterEnchantValue(hero, spellId, value);
}

}
