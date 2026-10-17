/*
 * PreparedReplaySnapshot.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedReplaySnapshot.h"
#include "CGameState.h"
#include <type_traits>

std::vector<std::byte> capturePreparedReplaySnapshot(CGameState & state, bool recording)
{
	if(!recording || !state.replayLog.empty())
		return {};
	// Complete the legacy log swap before acquiring the batch reservation.
	// Older plans remain invalidated by that real swap, including on failure.
	ReplayLog preserved = state.replayLog;
	try
	{
		return state.saveToMemory();
	}
	catch(...)
	{
		static_assert(std::is_nothrow_move_assignable_v<ReplayLog>);
		state.replayLog = std::move(preserved);
		throw;
	}
}
