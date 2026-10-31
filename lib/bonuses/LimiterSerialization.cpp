/*
 * LimiterSerialization.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "Limiters.h"

void ILimiter::validateAdjacencySerialization(bool) const {}

void ILimiter::validateBonusTypeSerialization(bool adjacencySupported, bool) const
{
	validateAdjacencySerialization(adjacencySupported);
}

void HasAnotherBonusLimiter::validateAdjacencySerialization(bool supported) const
{
	if(supported) return;
	if(type == BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION
		|| subtype.as<BonusTypeID>().toEnum() == BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION)
		throw std::runtime_error("Unsupported target MDR penetration limiter format");
}

void HasAnotherBonusLimiter::validateBonusTypeSerialization(bool adjacencySupported, bool healingReductionSupported) const
{
	validateAdjacencySerialization(adjacencySupported);
	if(!healingReductionSupported && (type == BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS
		|| subtype.as<BonusTypeID>().toEnum() == BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS))
		throw std::runtime_error("Unsupported incoming healing reduction limiter format");
}

void UnitAdjacentLimiter::validateAdjacencySerialization(bool supported) const
{
	if(!filter.isValid() || (!supported && !filter.isDefault())
		|| (filter.anyCreature && targetUnit.hasValue())
		|| (!filter.isDefault() && !filter.anyCreature && !targetUnit.hasValue()))
		throw std::runtime_error("Unsupported adjacency filter metadata");
}

void AggregateLimiter::validateAdjacencySerialization(bool supported) const
{
	for(const auto & limiter : limiters)
		if(limiter)
			limiter->validateAdjacencySerialization(supported);
}

void AggregateLimiter::validateBonusTypeSerialization(bool adjacencySupported, bool healingReductionSupported) const
{
	for(const auto & limiter : limiters)
		if(limiter) limiter->validateBonusTypeSerialization(adjacencySupported, healingReductionSupported);
}
