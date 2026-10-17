/*
 * NetworkLagPreparedReplay.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NetworkLagCompensator.h"
#include "../replay/PreparedClientBattleReplay.h"
#include <stdexcept>

std::unique_ptr<PreparedClientBattleReplay> NetworkLagCompensator::prepareBattleReplayEntry(
	const std::shared_ptr<CGameState> & world, BattleUnitsChanged & packet) const
{
	if(world != gameState)
		throw std::invalid_argument("Prediction and client replay worlds differ");
	return prepareClientBattleReplayEntry(world, packet, &replyState);
}
