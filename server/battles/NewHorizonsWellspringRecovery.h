/*
 * NewHorizonsWellspringRecovery.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/constants/EntityIdentifiers.h"
class CGameHandler;
class CGHeroInstance;
void applyWellspringVictoryRecovery(CGameHandler & handler, BattleID battleID, const CGHeroInstance * winner);
