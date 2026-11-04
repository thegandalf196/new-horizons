/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <cstdint>
#include <optional>
#include <span>
class CBattleInfoCallback;
class ServerCallback;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace arcaneInspiration
{
struct DLL_LINKAGE Reprisal
{
	uint32_t source = 0;
	uint32_t target = 0;
	int64_t rawDamage = 0;
};
/// Live pre-packet eligibility; amount is the native post-shield/pre-casualty receipt.
DLL_LINKAGE std::optional<Reprisal> captureReprisal(const CBattleInfoCallback &, const BattleAttackInfo &, int64_t inflictedDamage);
/// Shared target-only magical mitigation; no hero spell/caster output modifiers.
DLL_LINKAGE int64_t reprisalDamage(const CBattleInfoCallback &, const battle::Unit &, int64_t rawDamage);
/// Prepared injuries never re-enter attack reactions or spend a retaliation.
DLL_LINKAGE void publishReprisals(ServerCallback &, const CBattleInfoCallback &, std::span<const Reprisal>);
}
