/*
 * NetworkLagPreparedRetirement.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NetworkLagCompensator.h"
#include "PreparedNetworkLagRetirement.h"

std::unique_ptr<PreparedNetworkLagRetirement> NetworkLagCompensator::prepareReplyRetirement(
	const BattleReplyInspection & inspection, const std::unique_lock<std::mutex> & interfaceAccess)
{
	return replyState.prepareReplyRetirement(inspection, interfaceAccess);
}
