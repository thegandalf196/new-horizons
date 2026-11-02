/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CServerHandler.h"
#include "GameEngine.h"
#include "../lib/network/NetworkInterface.h"
#include "../lib/UnlockGuard.h"
#include <chrono>
#include <future>

void CServerHandler::waitForOwnedReceiveRetirement()
{
	// endGameplay already cancelled the dedicated controller and disconnected.
	// This context has ONE run thread. The checkpoint cannot execute before
	// its pending onPacket/Session/CClient call stacks have actually returned.
	auto & context = networkHandler->getContext();
	if(context.get_executor().running_in_this_thread() || !threadNetwork.joinable())
		return; // Existing packet-dispatched restart/end: never wait for ourselves.
	if(context.stopped())
	{ waitForNetworkThread(); return; }
	auto signal = std::make_shared<std::promise<void>>();
	auto completed = signal->get_future();
	boost::asio::post(context, [signal]() { signal->set_value(); }); // VALUES ONLY, no handler/client borrow.
	bool retired;
	{
		const auto wait = [&]() { return completed.wait_for(std::chrono::seconds(2)) == std::future_status::ready; };
		if(ENGINE)
		{
			auto unlocked = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
			retired = wait();
		}
		else retired = wait();
	}
	if(!retired)
	{
		// A stopped context can leave the posted value unexecuted. Reuse actual
		// stop/join retirement, never delete the client on timeout evidence.
		stopNetwork();
		waitForNetworkThread();
	}
}
