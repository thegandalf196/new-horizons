/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"

class BattleInfo;

/// Called by the replicated final deployment transition, before opening spells.
DLL_LINKAGE void captureBattleRecallAnchors(BattleInfo & battle);
