/*
 * NetworkPreparedWrites.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NetworkConnection.h"

boost::system::error_code NetworkConnection::drainSynchronousWrites()
{
	boost::system::error_code error;
	while(!dataToSend.empty())
	{
		boost::asio::write(*socket, boost::asio::buffer(dataToSend.front()), error);
		dataToSend.pop_front();
		if(error)
			break;
	}
	return error;
}

void NetworkConnection::startPreparedWrites()
{
	bool failed = false;
	{
		std::lock_guard lock(writeMutex);
		if(writeTerminal.load())
			failed = true; // An ordinary sync drain deferred reporting beyond its GC writer lock.
		else if(!writeInProgress && !dataToSend.empty())
		{
			if(asyncWritesEnabled)
				failed = !trySendData();
			else
			{
				const auto error = drainSynchronousWrites();
				if(error)
				{
					recordWriteFailure(NetworkWriteFailureInfo::Origin::SYNCHRONOUS_DRAIN, {}, error);
					failed = true;
				}
			}
		}
	}
	if(failed)
		finishWriteFailure();
}
