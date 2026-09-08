/*
 * NewHorizonsMasteryEffects.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMasteryEffects.h"
#include "../../bonuses/Bonus.h"
#include "../../bonuses/Limiters.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/army/CStackInstance.h"
#include "../../CCreatureHandler.h"
#include "../../callback/CPlayerSpecificInfoCallback.h"
#include "../../mapping/TerrainTile.h"
#include "../../pathfinder/TurnInfo.h"

namespace newHorizonsHeroes
{
int chooseMasteryForArmy(const MasteryOffer & offer, const CGHeroInstance & hero,
	const CPlayerSpecificInfoCallback * visible)
{
	validateMasteryOffer(offer);
	if(offer.hero != hero.id || offer.player != hero.getOwner())
		throw std::runtime_error("Mastery AI context does not own the offer");
	if(offer.skill == SecondarySkill::LOGISTICS)
	{
		if(!visible || visible->getPlayerID() != std::optional<PlayerColor>(hero.getOwner())
			|| visible->getHero(offer.hero) != &hero)
			throw std::runtime_error("Logistics mastery AI requires player-visible context");
		TurnInfoCache cache(&hero);
		TurnInfo movement(&cache, &hero, 0);
		const auto position = hero.visitablePos();
		const auto size = visible->getMapSize();
		bool coast = hero.inBoat();
		std::vector<int> terrainCosts;
		// A bounded LOCAL visible sample, not hidden map exploration or route
		// omniscience. Roads/native terrain already have no removable surcharge.
		constexpr int radius = 2;
		for(int dx = -radius; dx <= radius; ++dx)
			for(int dy = -radius; dy <= radius; ++dy)
			{
				const int3 tilePosition(position.x + dx, position.y + dy, position.z);
				if(tilePosition.x < 0 || tilePosition.y < 0 || tilePosition.x >= size.x || tilePosition.y >= size.y)
					continue;
				const auto * tile = visible->getTile(tilePosition, false);
				if(!tile || !tile->getTerrain()->isPassable())
					continue;
				coast |= tile->isWater();
				if(tile->isWater() || tile->blocked())
					continue;
				const int base = movement.getMovementCostBase();
				terrainCosts.push_back(tile->hasRoad() || movement.hasNoTerrainPenalty(tile->getTerrainID())
					? base : std::max(base, static_cast<int>(tile->getTerrain()->moveCost) - movement.getRoughTerrainDiscountValue()));
			}
		int best = -1;
		double bestScore = -1;
		for(int i = 0; i < static_cast<int>(offer.options.size()); ++i)
		{
			const auto & option = offer.options[i];
			double score = 0;
			if(option.effect == MasteryEffect::LOGISTICS_FORCED_MARCH && !hero.inBoat())
				score = static_cast<double>(option.magnitude) / std::max(1, movement.getMovePointsLimitLand());
			else if(option.effect == MasteryEffect::LOGISTICS_QUARTERMASTER)
				score = coast && !hero.hasBonusOfType(BonusType::FREE_SHIP_BOARDING) ? 1.0 : 0.0;
			else if(option.effect == MasteryEffect::LOGISTICS_PATHFINDER && !hero.inBoat())
			{
				for(const int cost : terrainCosts)
					score += static_cast<double>(std::min(option.magnitude, cost - movement.getMovementCostBase())) / std::max(1, cost);
				score /= std::max<size_t>(1, terrainCosts.size());
			}
			if(score > bestScore || (score == bestScore && option.id.value < offer.options[best].id.value))
			{
				best = i;
				bestScore = score;
			}
		}
		return best;
	}
	double escortValue = 0;
	double rangedValue = 0;
	for(const auto & [slot, stack] : hero.Slots())
	{
		const double value = static_cast<double>(stack->getCount()) * stack->getCreature()->getAIValue();
		escortValue += value;
		if(stack->hasBonusOfType(BonusType::SHOOTER))
			rangedValue += value;
	}
	const auto * machine = CreatureID(CreatureID::BALLISTA).toCreature();
	const double machineHealth = std::max(1, machine->getBaseHitPoints());
	// Special creatures can have zero adventure AI value; their health still
	// represents a real machine worth protecting rather than an inert option.
	const double machineValue = std::max(machineHealth, static_cast<double>(machine->getAIValue()));
	const double shots = 1.0 + std::max(0, hero.valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
		BonusSubtypeID(CreatureID(CreatureID::BALLISTA))));
	// Ranged escorts tend to hold position and exchange distant shots. A weak
	// escort instead leaves the machine exposed. These are declared provisional
	// playstyle estimates, not hidden-enemy knowledge or numerical balance claims.
	const double distantExchange = rangedValue / std::max(1.0, escortValue);
	constexpr double exposedActivations = 5.0;
	constexpr double occasionalSiegeValue = 0.1;
	int best = -1;
	double bestScore = -1;
	for(int i = 0; i < static_cast<int>(offer.options.size()); ++i)
	{
		const auto & option = offer.options[i];
		double score = 0;
		switch(option.effect)
		{
		case MasteryEffect::ARTILLERY_VOLLEY:
			score = option.magnitude / shots;
			break;
		case MasteryEffect::ARTILLERY_PRECISION:
			if(!hero.hasBonusOfType(BonusType::NO_DISTANCE_PENALTY))
				score += distantExchange;
			if(!hero.hasBonusOfType(BonusType::NO_WALL_PENALTY))
				score += occasionalSiegeValue;
			break;
		case MasteryEffect::ARTILLERY_REPAIR:
			score = std::min(static_cast<double>(option.magnitude), machineHealth) / machineHealth
				* exposedActivations * machineValue / (machineValue + escortValue);
			break;
		default:
			throw std::runtime_error("Non-Artillery effect reached Artillery AI valuation");
		}
		if(score > bestScore || (score == bestScore && option.id.value < offer.options[best].id.value))
		{
			best = i;
			bestScore = score;
		}
	}
	return best;
}

std::vector<std::shared_ptr<Bonus>> masteryBonuses(const MasteryOption & choice)
{
	validateMasteryOption(choice);
	std::vector<std::shared_ptr<Bonus>> result;
	const auto skill = masteryParentSkill(choice.effect);
	if(skill == SecondarySkill::LOGISTICS)
	{
		BonusType type = BonusType::MOVEMENT;
		if(choice.effect == MasteryEffect::LOGISTICS_QUARTERMASTER)
			type = BonusType::FREE_SHIP_BOARDING;
		else if(choice.effect == MasteryEffect::LOGISTICS_PATHFINDER)
			type = BonusType::ROUGH_TERRAIN_DISCOUNT;
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::HERO_MASTERY, choice.magnitude, BonusSourceID(skill));
		bonus->valType = BonusValueType::ADDITIVE_VALUE;
		if(choice.effect == MasteryEffect::LOGISTICS_FORCED_MARCH)
			bonus->subtype = BonusCustomSubtype::heroMovementLand;
		bonus->description.appendTextID(choice.nameTextId);
		result.push_back(std::move(bonus));
		return result;
	}
	const auto add = [&](BonusType type, int value, bool creatureLimited)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::HERO_MASTERY, value, BonusSourceID(skill));
		bonus->description.appendTextID(choice.nameTextId);
		if(creatureLimited)
		{
			auto limiter = std::make_shared<CCreatureTypeLimiter>();
			limiter->setCreature(CreatureID::BALLISTA);
			bonus->limiter = limiter;
		}
		else
			bonus->subtype = BonusSubtypeID(CreatureID(CreatureID::BALLISTA));
		result.push_back(std::move(bonus));
	};
	switch(choice.effect)
	{
	case MasteryEffect::ARTILLERY_VOLLEY:
		// This is a hero query, so a creature limiter would suppress the bonus.
		add(BonusType::HERO_GRANTS_ATTACKS, choice.magnitude, false);
		break;
	case MasteryEffect::ARTILLERY_PRECISION:
		add(BonusType::NO_DISTANCE_PENALTY, 1, true);
		add(BonusType::NO_WALL_PENALTY, 1, true);
		break;
	case MasteryEffect::ARTILLERY_REPAIR:
		add(BonusType::HP_REGENERATION, choice.magnitude, true);
		break;
	default:
		throw std::runtime_error("Non-Artillery effect reached Ballista bonus construction");
	}
	return result;
}
}
