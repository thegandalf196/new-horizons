/*
 * NetworkWriteFailure.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NetworkConnection.h"

void NetworkConnection::recordWriteFailure(NetworkWriteFailureInfo::Origin origin,
	std::exception_ptr cause, const boost::system::error_code & error) noexcept
{
	// Caller holds writeMutex. Publish terminal state before canceling operations:
	// their completion callbacks must not pop buffers or attempt later chunks.
	if(writeTerminal.load())
		return;
	writeFailure.origin = origin;
	writeFailure.cause = std::move(cause);
	writeFailure.error = error;
	writeTerminal.store(true);
}

bool NetworkConnection::trySendData() noexcept
{
	try
	{
		doSendData();
		return true;
	}
	catch(...)
	{
		recordWriteFailure(NetworkWriteFailureInfo::Origin::INITIATION, std::current_exception(), {});
		return false;
	}
}

bool NetworkConnection::hasTerminalWriteFailure() const noexcept
{
	return writeTerminal.load();
}

NetworkWriteStatus NetworkConnection::writeStatus()
{
	std::lock_guard lock(writeMutex);
	return {writeTerminal.load(), writeInProgress, writeOffset, dataToSend.size(), writeFailure};
}

void NetworkConnection::finishWriteFailure() noexcept
{
	{
		std::lock_guard lock(writeMutex);
		if(writeFailure.notificationAttempted)
			return;
		writeFailure.notificationAttempted = true;
	}
	// Both actions run OUTSIDE endpoint/writer locks. Close FIRST, so even a
	// persistently failing/allocating observer cannot prevent transport cleanup.
	std::exception_ptr cleanup;
	try
	{
		close();
	}
	catch(...)
	{
		cleanup = std::current_exception();
	}
	{
		std::lock_guard lock(writeMutex);
		writeFailure.cleanupFailure = std::move(cleanup);
		writeFailure.socketStillOpen = socket->is_open();
	}
	try
	{
		// Preallocated at connection construction. Never format what()/message()
		// while handling an allocation failure. Arbitrary observer exceptions are
		// retained separately, not relabeled as another transport failure.
		listener.onDisconnected(shared_from_this(), writeFailureMessage);
	}
	catch(...)
	{
		std::lock_guard lock(writeMutex);
		writeFailure.observationFailure = std::current_exception();
	}
}
