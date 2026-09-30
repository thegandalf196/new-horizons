/*
 * NewHorizonsBlink.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsBlink.h"

#include "ISpellMechanics.h"
#include "../GameConstants.h"
#include "../battle/AccessibilityInfo.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/Unit.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsBlink
{
namespace
{
const SpellID & blinkSpellId()
{
	static const SpellID id(SpellID::decode(std::string(SPELL_ID)));
	return id;
}
}

int radiusFor(const int32_t rawSpellPower, const int32_t coefficientBasisPoints,
	const int32_t warcastingBonusPercent)
{
	const int64_t scaledSpellPower = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		rawSpellPower, SPELL_POWER_PER_RADIUS, coefficientBasisPoints, warcastingBonusPercent, 0);
	return static_cast<int>(std::min<int64_t>(MAX_RADIUS, BASE_RADIUS + scaledSpellPower));
}

std::optional<Preview> preview(const spells::Mechanics & mechanics, const battle::Unit * unit)
{
	if(mechanics.getSpellId() != blinkSpellId() || !mechanics.usesNewHorizonsMagicV3())
		return std::nullopt;

	const auto * callback = mechanics.battle();
	if(!callback || !unit || !unit->alive() || !unit->isValidTarget(false) || unit->isGhost())
		return std::nullopt;

	const auto accessibility = callback->getAccessibility(unit);
	const auto origin = unit->getPosition();
	if(!origin.isValid())
		return std::nullopt;

	Preview result;
	result.radius = radiusFor(mechanics.getEffectPower(), mechanics.getSpellPowerCoefficientBasisPoints(),
		mechanics.getWarcastingBonusPercent());
	const auto * hero = mechanics.getHeroCaster();
	result.blinkmaster = hero && hero->hasActivePerk(std::string(CHAOS_SKILL), std::string(BLINKMASTER_PERK));

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex destination(index);
		if(destination == origin || BattleHex::getDistance(origin, destination) > result.radius)
			continue;
		if(accessibility.accessible(destination, unit))
			result.legalDestinations.push_back(destination);
	}

	if(result.legalDestinations.empty())
		return std::nullopt;
	return result;
}

BattleHex fartherDestination(const BattleHex & origin, const BattleHex & first, const BattleHex & second)
{
	const auto firstDistance = BattleHex::getDistance(origin, first);
	const auto secondDistance = BattleHex::getDistance(origin, second);
	if(firstDistance != secondDistance)
		return firstDistance > secondDistance ? first : second;
	return first.toInt() <= second.toInt() ? first : second;
}

std::vector<WeightedDestination> outcomeDistribution(const Preview & preview, const BattleHex & origin)
{
	std::vector<WeightedDestination> result;
	const auto & destinations = preview.legalDestinations;
	if(destinations.empty())
		return result;

	result.reserve(destinations.size());
	for(const auto & destination : destinations)
		result.push_back({destination, 0, 0});

	if(!preview.blinkmaster)
	{
		const auto totalWeight = static_cast<uint32_t>(destinations.size());
		for(auto & weighted : result)
		{
			weighted.weight = 1;
			weighted.totalWeight = totalWeight;
		}
		return result;
	}

	const auto totalWeight = static_cast<uint32_t>(destinations.size() * destinations.size());
	for(size_t first = 0; first < destinations.size(); ++first)
	{
		for(size_t second = 0; second < destinations.size(); ++second)
		{
			const auto firstDistance = BattleHex::getDistance(origin, destinations[first]);
			const auto secondDistance = BattleHex::getDistance(origin, destinations[second]);
			const size_t winnerIndex = secondDistance > firstDistance
				|| (secondDistance == firstDistance
					&& destinations[second].toInt() < destinations[first].toInt())
				? second : first;
			++result[winnerIndex].weight;
		}
	}

	for(auto & weighted : result)
		weighted.totalWeight = totalWeight;
	return result;
}
}
