/*
 * PreparedBattlePublication.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CVCMIServer.h"
#include "../lib/serializer/PreparedGameConnectionBatch.h"

std::unique_ptr<PreparedGameConnectionBatch> CVCMIServer::prepareBattlePublication(const BattleUnitsChanged & packet)
{
	// No applyPack routing change. This captures the REAL recipient owner and
	// enforces its actual event-loop thread, not a caller-provided readiness bit.
	return std::make_unique<PreparedGameConnectionBatch>(networkHandler->getContext(), activeConnections, packet);
}
