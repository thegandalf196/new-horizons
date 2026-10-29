/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
class CBattleInfoCallback;
class DamageCache;
class AttackPossibility;
namespace battle { class CUnitState; }
namespace petrifyingBloodAI
{
/// Borrowed candidate states remain owned by the enclosing preview/exchange until resolve.
struct AttemptIds { uint32_t attacker; uint32_t recipient; };
struct Pending { const battle::CUnitState * attacker; const battle::CUnitState * recipient; };
void capture(const std::shared_ptr<CBattleInfoCallback> & battle,
	battle::CUnitState & attacker, battle::CUnitState & recipient,
	int64_t damage, bool ranged, bool physical, std::vector<Pending> & pending);
float resolve(const std::shared_ptr<CBattleInfoCallback> & battle,
	const battle::CUnitState & reference, const std::vector<Pending> & pending, DamageCache & cache);
/// Defer nested physical counters to the complete accepted shooting command.
void complete(AttackPossibility & preview, const std::shared_ptr<CBattleInfoCallback> & battle,
	const std::vector<Pending> & pending, bool defer, DamageCache & cache);
/// One-activation expected control value, separate from damage and deterministic status.
/// Spends only the recipient copy's round receipt; never installs guaranteed Petrification.
float controlValue(const std::shared_ptr<CBattleInfoCallback> & battle,
	battle::CUnitState & attacker, battle::CUnitState & recipient,
	int64_t damage, bool ranged, bool physical, DamageCache & cache);
}
