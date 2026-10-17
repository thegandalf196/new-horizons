/*
 * PreparedNetworkSchedule.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PreparedNetworkBatchState.h"
#include <boost/asio/post.hpp>

void PreparedNetworkBatch::State::schedule()
{
	// ALL endpoint locks are already held. A handler cannot observe a pending
	// frame while a later post/allocation may still fail. Cancellation precedes
	// lock release on every failure path. No executor is invoked at acceptance.
	for(const auto & delivery : deliveries)
	{
		if(delivery.tcp)
		{
			boost::asio::post(delivery.tcp->context, [endpoint = delivery.tcp, gate = gate]()
			{
				bool accepted;
				{
					std::lock_guard lock(endpoint->writeMutex);
					accepted = gate->phase.load() == PreparedNetworkGate::Phase::ACCEPTED;
				}
				if(accepted)
					endpoint->startPreparedWrites();
			});
		}
		else
		{
			boost::asio::post(delivery.receiver->deliveryStrand,
				[endpoint = delivery.receiver, bytes = delivery.bytes, gate = gate]()
			{
				bool deliver;
				{
					std::lock_guard lock(endpoint->deliveryMutex);
					deliver = gate->phase.load() == PreparedNetworkGate::Phase::ACCEPTED && endpoint->connectionActive;
				}
				if(deliver)
					endpoint->listener.onPacketReceived(endpoint, *bytes);
			});
		}
	}
	scheduled = true;
}
