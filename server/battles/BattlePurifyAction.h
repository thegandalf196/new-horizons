/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
#include <vector>
class CGameHandler;
class CBattleInfoCallback;
class CGHeroInstance;
class BattleAction;
std::vector<uint32_t> applyPurifyAction(CGameHandler & gameHandler, const CBattleInfoCallback & battle,
	const BattleAction & action, const CGHeroInstance * hero, bool purifyingMandate);
