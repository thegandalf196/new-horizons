/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsRunicReflection.h"
#include "ISpellMechanics.h"
#include "Problem.h"
#include "NewHorizonsSorcery.h"
#include "effects/Effect.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/Unit.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/Limiters.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/CHero.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <stdexcept>

namespace newHorizonsRunicReflection
{
int reflectionChance(int effectiveResistance, int nativeReflection)
{
	// Complete resistance is ordinary immunity, never a reflection band.
	if(effectiveResistance >= 100) return 0;
	return std::min(std::clamp(effectiveResistance, 0, 100), std::clamp(nativeReflection, 0, 100));
}
Outcome outcome(int effectiveResistance, int nativeReflection, int acceptedSample, bool legalReflection)
{
	if(acceptedSample < 0 || acceptedSample >= 100)
		throw std::invalid_argument("Resistance sample is outside its ordinary die");
	if(acceptedSample >= std::clamp(effectiveResistance, 0, 100))
		return Outcome::Apply;
	return legalReflection && acceptedSample < reflectionChance(effectiveResistance, nativeReflection)
		? Outcome::Reflect : Outcome::Resist;
}
int nativeChance(const battle::Unit & unit)
{
	return std::clamp(unit.valOfBonuses(BonusType::NATIVE_RESISTANCE_REFLECTION), 0, 100);
}
std::vector<const battle::Unit *> legalRecipients(const spells::Mechanics & reflected,
	std::span<const spells::effects::Effect * const> effects, const battle::Unit * reflector)
{
	if(!reflected.isMagicMirror())
		throw std::invalid_argument("Runic recipient pool requires a reflected cast view");
	if(effects.empty() || std::ranges::any_of(effects, [](const auto * effect)
		{ return !effect || effect->resistanceShareKind() == spells::effects::ResistanceShareKind::Unsupported; })) return {};
	std::vector<const battle::Unit *> result;
	const auto * callback = reflected.battle();
	for(const auto * unit : callback->battleGetAllUnits(false))
	{
		if(!unit->isValidTarget(false) || unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| callback->battleGetOwner(unit) != reflected.getCasterColor()
			|| !reflected.isReceptive(unit) || newHorizonsSorcery::isSpellLocked(*unit)) continue;
		const spells::Target destination{spells::Destination(unit)};
		if(std::ranges::all_of(effects, [&](const auto * effect)
		{
			spells::Target aim = destination;
			if(effect->resistanceShareKind() == spells::effects::ResistanceShareKind::LifeDrain)
			{
				if(!reflector || !reflector->isValidTarget(false) || newHorizonsSorcery::isSpellLocked(*reflector)) return false;
				aim.emplace_back(reflector);
			}
			spells::detail::ProblemImpl problem;
			return effect->applicableTarget(problem, &reflected, aim)
				&& effect->filterTarget(&reflected, aim).size() == aim.size();
		})) result.push_back(unit);
	}
	std::ranges::sort(result, {}, &battle::Unit::unitId);
	return result;
}

std::vector<std::shared_ptr<Bonus>> preparedBonuses(const CGHeroInstance & hero)
{
	if(!hero.getHeroType() || hero.getHeroType()->getJsonKey() != "core:ufretin"
		|| !newHorizonsHeroes::hasOrdinaryCreatureSpecialty(&hero))
		return {};
	const auto & policy = hero.getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	if(policy[POLICY_KEY].isNull())
		return {};
	if(policy[POLICY_KEY].getType() != JsonNode::JsonType::DATA_INTEGER || policy[POLICY_KEY].Integer() != 1)
		throw std::runtime_error("Invalid captured native resistance reflection policy");
	std::vector<std::shared_ptr<Bonus>> result;
	for(const auto & [key, percent] : {std::pair{"core:dwarf", 20}, std::pair{"core:battleDwarf", 40}})
	{
		const auto creature = CreatureID(CreatureID::decode(key));
		if(!creature.hasValue())
			throw std::runtime_error("Native resistance reflection family is not registered");
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::NATIVE_RESISTANCE_REFLECTION,
			BonusSource::HERO_SPECIAL, percent, BonusSourceID(hero.getHeroTypeID()));
		bonus->limiter = std::make_shared<CCreatureTypeLimiter>(*creature.toCreature(), true);
		// The upgraded creature matches both family limiters. These markers
		// describe one native source, so only the strongest applicable one counts.
		bonus->stacking = "nativeResistanceReflection";
		result.push_back(std::move(bonus));
	}
	return result;
}
}
