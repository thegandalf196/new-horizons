/*
 * NewHorizonsRealityWarp.cpp, part of VCMI / New Horizons
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsRealityWarp.h"

namespace newHorizonsRealityWarp
{
namespace
{
EffectBundle copyForRecipient(const EffectBundle & source, PlayerColor recipientOwner)
{
	auto transferred = source;
	for(auto & bonus : transferred.bonuses)
		if(bonus.spellCasterOwner != PlayerColor::CANNOT_DETERMINE
			&& recipientOwner != PlayerColor::CANNOT_DETERMINE)
			bonus.appliedByEnemy = bonus.spellCasterOwner != recipientOwner;
	return transferred;
}
}

ExchangePlan planExchange(const std::vector<EffectBundle> & firstBundles, PlayerColor firstOwner,
	const std::vector<EffectBundle> & secondBundles, PlayerColor secondOwner,
	const RecipientLegality & recipientAllows)
{
	ExchangePlan result;
	result.first.reserve(firstBundles.size() + secondBundles.size());
	result.second.reserve(firstBundles.size() + secondBundles.size());

	std::vector<EffectBundle> movingToFirst;
	std::vector<EffectBundle> movingToSecond;
	movingToFirst.reserve(secondBundles.size());
	movingToSecond.reserve(firstBundles.size());

	const auto route = [&recipientAllows](const std::vector<EffectBundle> & sourceBundles,
		RecipientSide destinationSide, PlayerColor destinationOwner,
		std::vector<EffectBundle> & stay, std::vector<EffectBundle> & moving)
	{
		for(const auto & bundle : sourceBundles)
		{
			if(!bundle.transferable || !recipientAllows
				|| !recipientAllows(bundle, destinationSide, destinationOwner))
			{
				stay.push_back(bundle);
				continue;
			}

			moving.push_back(copyForRecipient(bundle, destinationOwner));
		}
	};

	// Decide both directions from the original snapshots before assembling either
	// result, so a transferred bundle cannot affect legality in the other direction.
	route(firstBundles, RecipientSide::SECOND, secondOwner, result.first, movingToSecond);
	route(secondBundles, RecipientSide::FIRST, firstOwner, result.second, movingToFirst);

	result.first.insert(result.first.end(), movingToFirst.begin(), movingToFirst.end());
	result.second.insert(result.second.end(), movingToSecond.begin(), movingToSecond.end());
	return result;
}
}
