/*
 * ClientReceiveAccess.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../StdInc.h"
#include "ClientReceiveAccess.h"
#include "PreparedClientBattleReplay.h"
#include "../netlag/PreparedNetworkLagRetirement.h"
#include <stdexcept>

ClientReceiveAccess::ClientReceiveAccess(std::unique_lock<std::mutex> && access, std::mutex * exactMutex)
	: interfaceAccess(std::move(access)), expectedMutex(exactMutex)
{
}

ClientReceiveAccess::~ClientReceiveAccess() = default;

bool ClientReceiveAccess::heldOnThisThread() const noexcept
{
	return thread == std::this_thread::get_id() && expectedMutex
		&& interfaceAccess.owns_lock() && interfaceAccess.mutex() == expectedMutex;
}

bool ClientReceiveAccess::owns(const PreparedClientBattleReplay & entry) const noexcept
{
	return heldOnThisThread() && child.get() == &entry;
}

void ClientReceiveAccess::requireEmpty() const
{
	if(!heldOnThisThread() || child)
		throw std::logic_error("Prepared receive requires its original held interface interval and no previous child");
}

PreparedClientBattleReplay & ClientReceiveAccess::prepareConsumed(std::shared_ptr<CGameState> world,
	BattleUnitsChanged & packet, NetworkLagReplyState & predictions)
{
	requireEmpty(); // BEFORE state/native capture or replay snapshot/writer
	auto candidate = prepareClientBattleReplayEntry(std::move(world), packet, &predictions);
	if(candidate->replyAction() != BattleReplyInspection::Result::CONSUMED)
		throw std::logic_error("This acceptance owner requires an actual matched reply, not an incoming or rollback program");
	auto queue = predictions.prepareReplyRetirement(*candidate->inspection, interfaceAccess);
	if(!candidate->ready() || !queue->ready())
		throw std::runtime_error("Consumed reply changed during preparation");
	candidate->receiveAccess = this;
	candidate->queueRetirement = std::move(queue);
	child = std::move(candidate);
	return *child;
}
