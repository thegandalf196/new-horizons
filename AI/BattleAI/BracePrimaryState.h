/*
 * BracePrimaryState.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>

class HypotheticBattle;

namespace attackPreview
{
/// Shared preview/replay boundary for an already-applied physical Brace primary.
/// IDs identify the same branch's retained striker and recipient. Capture their
/// health before damage; call before reactive Last Stand Defend. No continuation,
/// damage roll, ordinary-retaliation spend or completed-action claim is made.
void finishBracePrimaryState(HypotheticBattle & branch, uint32_t actorId,
	uint32_t victimId, int64_t victimHealthBefore);
}
