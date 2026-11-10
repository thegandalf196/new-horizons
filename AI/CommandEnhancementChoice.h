/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../lib/entities/hero/NewHorizonsCommandOffer.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/army/CStackInstance.h"
#include "../lib/CCreatureHandler.h"
#include <algorithm>
#include <span>
#include <stdexcept>
#include <vector>

namespace commandAI
{
/// One visible occupied slot. No enemy information, future recruitment or planner.
struct EnhancementStack
{
	SlotID slot;
	double value = 0;
	int initiative = 1, speed = 1;
	bool shooter = false, unlimitedRetaliations = false;
	bool luckEligible = true, moraleEligible = true;
	int luck = 0, morale = 0;
};

inline double enhancementValue(const newHorizonsHeroes::CommandEnhancementOption & option,
	const EnhancementStack & stack)
{
	using E = newHorizonsHeroes::CommandEnhancement;
	const double amount = static_cast<double>(option.contribution);
	switch(option.enhancement)
	{
	case E::READINESS: return amount / std::max(1, stack.initiative);
	case E::MOBILITY: return amount / std::max(1, stack.speed) * (stack.shooter ? 0.25 : 1.0);
	case E::COUNTERATTACK: return stack.unlimitedRetaliations ? 0 : amount * (stack.shooter ? 0.05 : 0.20);
	case E::WEAPON_DRILL:
	case E::GUARD_DRILL: return amount * 0.05;
	case E::FORTUNE: return stack.luckEligible ? std::min(amount, static_cast<double>(std::max(0, 10 - stack.luck))) * 0.04 : 0;
	case E::COURAGE: return stack.moraleEligible ? std::min(amount, static_cast<double>(std::max(0, 10 - stack.morale))) * 0.03 : 0;
	case E::ENDURANCE: return amount / 100.0;
	case E::WARDING: return std::min(amount, 95.0) / 200.0;
	}
	throw std::invalid_argument("Unknown Command enhancement in AI offer");
}

/// Nine captured options x at most seven slots. Stable ties retain offer order.
/// Empty recipients use the same neutral future-occupant prior, never a search.
inline int chooseEnhancement(const newHorizonsHeroes::CommandEnhancementOffer & offer,
	std::span<const EnhancementStack> army)
{
	if(army.size() > GameConstants::ARMY_SIZE)
		throw std::invalid_argument("Command choice requires a seven-slot army");
	int best = 0;
	double bestValue = -1;
	for(size_t i = 0; i < offer.options.size(); ++i)
	{
		const auto & option = offer.options[i];
		if(option.contribution < 0)
			throw std::invalid_argument("Negative Command enhancement contribution");
		double score = 0, weight = 0;
		for(const auto & stack : army)
		{
			if(!newHorizonsHeroes::commandRecipientContains(offer.recipient, stack.slot) || stack.value <= 0)
				continue;
			score += stack.value * enhancementValue(option, stack);
			weight += stack.value;
		}
		if(weight == 0)
			score = enhancementValue(option, EnhancementStack{SlotID(0), 1, 5, 5});
		else
			score /= weight;
		if(score > bestValue) { best = static_cast<int>(i); bestValue = score; }
	}
	return best;
}

inline int chooseEnhancement(const newHorizonsHeroes::CommandEnhancementOffer & offer, const CGHeroInstance & hero)
{
	if(offer.hero != hero.id || offer.player != hero.getOwner())
		throw std::invalid_argument("Command AI context does not own the offer");
	std::vector<EnhancementStack> army;
	for(const auto & [slot, stack] : hero.Slots())
	{
		if(!newHorizonsHeroes::commandRecipientContains(offer.recipient, slot)) continue;
		const auto * creature = stack->getCreature();
		army.push_back({slot, static_cast<double>(stack->getCount()) * creature->getAIValue(),
			creature->getBaseInitiative(), creature->getBaseSpeed(), stack->hasBonusOfType(BonusType::SHOOTER),
			stack->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS), !stack->hasBonusOfType(BonusType::NO_LUCK),
			!stack->hasBonusOfType(BonusType::NO_MORALE) && !stack->hasBonusOfType(BonusType::UNDEAD)
				&& !stack->hasBonusOfType(BonusType::NON_LIVING),
			stack->valOfBonuses(BonusType::LUCK), stack->valOfBonuses(BonusType::MORALE)});
	}
	return chooseEnhancement(offer, army);
}
}
