/*
 * InternalDelivery.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NetworkConnection.h"
#include <boost/asio/post.hpp>

void InternalConnection::receivePacket(const std::vector<std::byte> & message)
{
	std::lock_guard lock(deliveryMutex);
	boost::asio::post(deliveryStrand, [self = shared_from_this(), message]()
	{
		bool active;
		{
			std::lock_guard lock(self->deliveryMutex);
			active = self->connectionActive;
		}
		if(active)
			self->listener.onPacketReceived(self, message);
	});
}

void InternalConnection::disconnect()
{
	std::lock_guard lock(deliveryMutex);
	boost::asio::post(deliveryStrand, [self = shared_from_this()]()
	{
		// Preserve the old notification-before-clearing order. Never invoke a
		// client/server callback with the endpoint mutex held.
		self->listener.onDisconnected(self, "Internal connection has been terminated");
		std::lock_guard lock(self->deliveryMutex);
		self->otherSideWeak.reset();
		self->connectionActive = false;
#ifdef NH_PERF_EXPERIMENTS
		self->experimentalOpen.store(false);
#endif
	});
}

void InternalConnection::connectTo(std::shared_ptr<IInternalConnection> connection)
{
	std::lock_guard lock(deliveryMutex);
#ifdef NH_PERF_EXPERIMENTS
	if(experimentalPeerBound)
		throw std::logic_error("Experimental internal endpoint cannot be rebound");
	experimentalPeer = connection;
	experimentalPeerBound = true;
	experimentalOpen.store(true);
#endif
	otherSideWeak = connection;
	connectionActive = true;
}

void InternalConnection::sendPacket(const std::vector<std::byte> & message)
{
	std::shared_ptr<IInternalConnection> otherSide;
	{
		std::lock_guard lock(deliveryMutex);
		otherSide = otherSideWeak.lock();
	}
	if(otherSide)
		otherSide->receivePacket(message);
}

void InternalConnection::setAsyncWritesEnabled(bool on)
{
	// no-op
}

void InternalConnection::close()
{
	std::shared_ptr<IInternalConnection> otherSide;
	{
		std::lock_guard lock(deliveryMutex);
#ifdef NH_PERF_EXPERIMENTS
		experimentalOpen.store(false);
#endif
		otherSide = otherSideWeak.lock();
	}
	if(otherSide)
		otherSide->disconnect();
	std::lock_guard lock(deliveryMutex);
	otherSideWeak.reset();
	connectionActive = false;
}
