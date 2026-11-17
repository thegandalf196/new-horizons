/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include "../../lib/battle/UnitShieldState.h"
#include "../../lib/battle/RedemptionEvent.h"
class CGameHandler;
class CBattleInfoCallback;
struct BattleStackAttacked;
namespace battle { class Unit; }
namespace battleConsecratedArms
{
struct AttackContext
{
	std::map<uint32_t, battle::VowCasualtyQuota> vow;
	battle::RedemptionEvent redemption;
};
battle::DamageContext damageContext(AttackContext & attack, const battle::Unit & target, bool physical);
struct Judgment
{
	uint32_t source = 0;
	uint32_t target = 0;
	int64_t rawMagicalDamage = 0;
	battle::VowCasualtyQuota * vow = nullptr;
};
std::vector<Judgment> prepareJudgment(const CBattleInfoCallback & battle,
	const battle::Unit & source, bool ordinaryStrike, const std::vector<BattleStackAttacked> & hits,
	AttackContext & attack);
void applyJudgment(CGameHandler & handler, const CBattleInfoCallback & battle,
	const std::vector<Judgment> & prepared, AttackContext & attack);
bool fearlessAdvance(CGameHandler & handler, const CBattleInfoCallback & battle,
	const battle::Unit & attacker);
}
