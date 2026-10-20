/*
 * BattleEndDispatch.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <algorithm>
#include <utility>

// Terminal delivery stays synchronous on the caller's thread. Capture every
// recipient before invoking the primary: it may unregister and release an AI.
// Registrations made by callbacks belong to subsequent events, not this result.
template<typename PrimaryMap, typename AdditionalMap, typename Players, typename Deliver>
void dispatchBattleEndToSnapshot(const PrimaryMap & primary, const AdditionalMap & additional,
	const Players & players, Deliver && deliver)
{
	typename AdditionalMap::mapped_type recipients;
	const auto append = [&recipients](typename AdditionalMap::mapped_type::value_type receiver)
	{
		if(std::find(recipients.begin(), recipients.end(), receiver) == recipients.end())
			recipients.push_back(std::move(receiver));
	};
	for(const auto & player : players)
	{
		if(const auto found = primary.find(player); found != primary.end())
			append(found->second);
		if(const auto found = additional.find(player); found != additional.end())
			for(const auto & receiver : found->second)
				append(receiver);
	}
	for(const auto & receiver : recipients)
		deliver(*receiver);
}
