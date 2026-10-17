/*
 * ClientReceiveConsumedFactory.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../StdInc.h"
#include "ClientReceiveAccess.h"
#include "ClientPacketReceiveSession.h"
#include "PreparedClientBattleReplay.h"
#include "../netlag/NetworkLagCompensator.h"
#include "../netlag/PreparedNetworkLagRetirement.h"
#include <stdexcept>

PreparedClientBattleReplay & ClientReceiveAccess::prepareConsumed(std::shared_ptr<CGameState> world,
	BattleUnitsChanged & packet, NetworkLagCompensator & predictions, ClientPacketReceiveSession & session)
{
	requireEmpty();
	if(!session.ready(packet))
		throw std::logic_error("Native session changed before replay capture");
	auto candidate = predictions.prepareBattleReplayEntry(world, packet);
	if(candidate->replyAction() != BattleReplyInspection::Result::CONSUMED)
		throw std::logic_error("Matched acceptance cannot substitute for actual mismatch rollback or incoming apply");
	auto queue = predictions.prepareReplyRetirement(*candidate->inspection, interfaceAccess);
	if(!session.ready(packet) || !candidate->ready() || !queue->ready())
		throw std::runtime_error("Native consumed session changed during preparation");
	candidate->receiveAccess = this;
	candidate->receiveSession = &session;
	candidate->queueRetirement = std::move(queue);
	child = std::move(candidate);
	return *child;
}
