/*
 * PreparedReplaySnapshot.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "ReplayPayload.h"
#include <cstddef>
#include <vector>
class CGameState;

/// Shared typed pre-writer snapshot owner; restores the real empty log on throw.
DLL_LINKAGE ReplayPayload capturePreparedReplaySnapshot(CGameState & state, bool recording);
