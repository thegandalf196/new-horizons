/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "LeadershipEvaluation.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace NK2AI::leadershipAI
{
uint64_t addPower(uint64_t left, uint64_t right)
{
	return right > std::numeric_limits<uint64_t>::max() - left
		? std::numeric_limits<uint64_t>::max() : left + right;
}

uint64_t scalePower(uint64_t power, long double multiplier)
{
	if(!std::isfinite(multiplier) || multiplier < 0)
		throw std::invalid_argument("Invalid army strength multiplier");
	const long double value = power * multiplier;
	return value >= static_cast<long double>(std::numeric_limits<uint64_t>::max())
		? std::numeric_limits<uint64_t>::max() : static_cast<uint64_t>(value);
}

uint64_t scalePowerRatio(uint64_t power, uint32_t numerator, uint32_t denominator)
{
	if(denominator == 0) throw std::invalid_argument("Army strength ratio requires a denominator");
	const auto quotient = power / denominator;
	if(numerator && quotient > std::numeric_limits<uint64_t>::max() / numerator)
		return std::numeric_limits<uint64_t>::max();
	// The two 32-bit factors keep the remainder product representable.
	return addPower(quotient * numerator, (power % denominator) * numerator / denominator);
}

uint64_t powerWithEfficiency(uint64_t unrestrictedPower, double efficiency)
{
	if(!std::isfinite(efficiency) || efficiency < 0 || efficiency > 1)
		throw std::invalid_argument("Invalid Leadership efficiency in army valuation");
	// Army strength is approximately sqrt(offensive output * durability).
	// Only output changes: multiplying the entire value by efficiency would
	// incorrectly devalue the full health of over-capacity creatures twice.
	return scalePower(unrestrictedPower, std::sqrt(static_cast<long double>(efficiency)));
}

uint64_t stackPower(const CGHeroInstance * hero, CreatureID creature, int64_t count,
	const CCreatureSet * composition)
{
	if(count <= 0 || !creature.hasValue() || !creature.toCreature())
		return 0;
	const long double raw = static_cast<long double>(creature.toCreature()->getAIValue()) * count;
	const auto unrestricted = raw >= static_cast<long double>(std::numeric_limits<uint64_t>::max())
		? std::numeric_limits<uint64_t>::max() : static_cast<uint64_t>(raw);
	return powerWithEfficiency(unrestricted,
		newHorizonsHeroes::stackLeadershipEfficiency(hero, creature, count, composition));
}

uint64_t armyStrength(const CGHeroInstance * hero, const CCreatureSet * army, int fortLevel)
{
	if(!army)
		return 0;
	if(!hero || !newHorizonsHeroes::usesLeadershipEfficiency(hero->getCapabilityRules()))
		return army->getArmyStrength(fortLevel);
	const auto composition = newHorizonsHeroes::captureLeadershipComposition(*hero, army);
	long double adjusted = 0;
	for(const auto & [slot, stack] : army->Slots())
	{
		auto power = stack->getPower();
		if(fortLevel > 0 && !stack->hasBonusOfType(BonusType::FLYING))
		{
			power /= fortLevel;
			if(!stack->hasBonusOfType(BonusType::SHOOTER)) power /= fortLevel;
		}
		const auto readout = newHorizonsHeroes::leadershipStackReadout(hero,
			stack->getCreatureID(), stack->getCount(), &composition, army);
		adjusted += powerWithEfficiency(power, readout.efficiency);
	}
	return adjusted >= static_cast<long double>(std::numeric_limits<uint64_t>::max())
		? std::numeric_limits<uint64_t>::max() : static_cast<uint64_t>(adjusted);
}

float modifierGain(const CGHeroInstance & hero, const newHorizonsHeroes::LeadershipModifiers & modifiers)
{
	if(!newHorizonsHeroes::usesLeadershipEfficiency(hero.getCapabilityRules()))
		return 0;
	const auto composition = newHorizonsHeroes::captureLeadershipComposition(hero);
	long double total = 0, gain = 0;
	for(const auto & [slot, stack] : hero.Slots())
	{
		const auto power = stack->getPower();
		const auto before = newHorizonsHeroes::leadershipStackReadout(&hero,
			stack->getCreatureID(), stack->getCount(), &composition);
		const auto after = newHorizonsHeroes::leadershipStackReadout(&hero,
			stack->getCreatureID(), stack->getCount(), &composition, nullptr, &modifiers);
		total += power;
		gain += static_cast<long double>(powerWithEfficiency(power, after.efficiency))
			- powerWithEfficiency(power, before.efficiency);
	}
	return total > 0 ? static_cast<float>(gain / total) : 0;
}

float perkGain(const CGHeroInstance & hero, const newHorizonsHeroes::PerkOfferCandidate & candidate)
{
	auto modifiers = newHorizonsHeroes::captureLeadershipModifiers(hero);
	if(candidate.selection.skillId == "new-horizons:authority"
		&& candidate.requiredRank >= 1 && candidate.requiredRank <= 3)
	{
		const auto separator = candidate.selection.perkId.rfind('.');
		if(separator == std::string::npos) return 0;
		modifiers.authorityPerks[candidate.requiredRank - 1] = candidate.selection.perkId.substr(separator + 1);
	}
	else if(candidate.selection.perkId == "new-horizons:necromancy.lordOfTheDead")
		modifiers.lordOfTheDead = true;
	else
		return 0;
	return modifierGain(hero, modifiers);
}
}
