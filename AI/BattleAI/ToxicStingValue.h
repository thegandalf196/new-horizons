/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include "../../lib/battle/NewHorizonsToxicSting.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/constants/EntityIdentifiers.h"
class AttackPossibility;
struct BattleAttackInfo;
class CBattleInfoCallback;
class DamageCache;
class HypotheticBattle;
namespace battle { class Unit; }
namespace toxicStingAI
{
/// Cheap visible eligibility only; no synthetic hit, path or utility estimate.
bool requiresPreview(const CBattleInfoCallback & battle, const battle::Unit & source,
	const battle::Unit & requested, const battle::Unit & resolved);
std::optional<newHorizonsToxicSting::Plan> prepare(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, int64_t incoming, battle::DamageProvenance provenance);
struct Budget
{
	int64_t base = 0;
	int remaining = 0;
	int32_t source = -1;
	auto operator<=>(const Budget &) const = default;
};
/// Metadata only. Equivalent strongest/refresh outcomes coalesce; units are never branched.
struct Outcome
{
	bool available = false;
	std::map<uint32_t, Budget> budgets;
	auto operator<=>(const Outcome &) const = default;
};
class Opportunities
{
	struct Ledger
	{
		std::map<Outcome, double> outcomes;
		std::map<uint32_t, Budget> observed;
		std::map<uint32_t, uint32_t> sources;
	};
	std::map<BattleSide, Ledger> ledgers;
	float preparedValue(const std::shared_ptr<HypotheticBattle> & battle, const BattleAttackInfo & attack,
		const std::optional<newHorizonsToxicSting::Plan> & plan, DamageCache & cache, bool measure = true);
public:
	/// Call before generic reactions, with actual primary BODY loss. Source's native
	/// afterAttack resource transition is queried on a detached copy only.
	float value(const std::shared_ptr<HypotheticBattle> & battle,
		const BattleAttackInfo & attack, int64_t bodyLoss, DamageCache & cache);
	void score(AttackPossibility & preview, const std::shared_ptr<HypotheticBattle> & battle,
		const BattleAttackInfo & attack, const std::optional<newHorizonsToxicSting::Plan> & plan, DamageCache & cache);
	/// Price the final surviving Poison outcomes once, after the whole command.
	void complete(AttackPossibility & preview, const std::shared_ptr<CBattleInfoCallback> & battle, DamageCache & cache);
	void scoreCounter(AttackPossibility & preview, const std::shared_ptr<HypotheticBattle> & battle,
		const battle::Unit & source, const battle::Unit & target,
		const std::optional<newHorizonsToxicSting::Plan> & plan, bool physical, DamageCache & cache);
};
}
