/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/constants/EntityIdentifiers.h"
class CGameHandler;
class CGHeroInstance;
void applyWisdomBattleRecovery(CGameHandler & game, BattleID battle, const CGHeroInstance * hero,
	int64_t spent, bool retained, bool victorious, bool completedCast);
