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

namespace newHorizonsHeroes
{
int chooseMasteryForArmy(const MasteryOffer & offer, const CGHeroInstance & hero)
{
	validateMasteryOffer(offer);
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
	const auto add = [&](BonusType type, int value, bool creatureLimited)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::HERO_MASTERY, value, BonusSourceID(SecondarySkill(SecondarySkill::ARTILLERY)));
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
	}
	return result;
}
}
