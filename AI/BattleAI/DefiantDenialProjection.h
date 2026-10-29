/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/battle/ArmorerDefiantState.h"
class HypotheticBattle;
class CBattleInfoCallback;
class CSelector;
#include <utility>
struct BattleAttackInfo;
struct FortuneStrikeProjection;
struct DefiantDenialProjection;
namespace battleDefiantAI
{
void applyNoQuarterOrDefiant(HypotheticBattle & battle, const BattleAttackInfo & attack,
	uint32_t target, int32_t moraleActivations, FortuneStrikeProjection & receipt);
std::pair<bool, bool> projectMeleeDenials(HypotheticBattle * battle, const BattleAttackInfo & attack,
	bool blocked, bool admitted, const CSelector & firstStrike);
bool needsProjection(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
std::pair<bool, bool> projectMeleeDenials(HypotheticBattle * battle, const BattleAttackInfo & attack,
	bool blocked, bool admitted, const CSelector & firstStrike, FortuneStrikeProjection & receipt);
bool project(HypotheticBattle & battle, const BattleAttackInfo & attack,
	newHorizonsArmorer::DefiantDenialCause cause, FortuneStrikeProjection & strike);
bool project(HypotheticBattle & battle, const BattleAttackInfo & attack,
	newHorizonsArmorer::DefiantDenialCause cause);
void replay(HypotheticBattle & battle, const DefiantDenialProjection & receipt);
}
