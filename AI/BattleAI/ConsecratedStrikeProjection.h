/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/UnitShieldState.h"
#include "../../lib/battle/RedemptionEvent.h"
#include "../../lib/battle/CreatureAbility.h"
#include "../../lib/battle/ConsecratedArmsCombat.h"
#include <map>
#include <memory>
#include <vector>
class HypotheticBattle;
class CBattleInfoCallback;
class StackWithBonuses;
class DamageCache;
class CSpell;
struct BattleAttackInfo;
namespace battle { class CUnitState; }
namespace consecratedArmsAI
{
bool needsStrikeProjection(const CBattleInfoCallback &, const battle::Unit *, const battle::Unit *);
/// Expected counter correction only; the existing nominal counter state stays
/// isolated. Call after the native counter eligibility gates, not for all hits.
DLL_LINKAGE float fearlessCounterCorrection(const battle::Unit &, float utility, float otherFailureProbability = 1);
/// Restore owned peer shield values for selected snapshot-only attack routes.
DLL_LINKAGE void restoreShieldState(HypotheticBattle &, const HypotheticBattle &);
/// Explicit completed-action sites only; Wait, counterstrikes, forfeitures and
/// speculative no-target activation cleanup do not call this boundary.
DLL_LINKAGE void completeObservedAction(HypotheticBattle &, uint32_t actor);
DLL_LINKAGE int afflictionResistanceBP(const CBattleInfoCallback &, const battle::Unit & source,
	const battle::Unit & target, creatureAbility::Native);
DLL_LINKAGE int afflictionResistanceBP(const CBattleInfoCallback &, const battle::Unit & source,
	const battle::Unit & target, const CSpell &);
class StrikeProjection
{
public:
	struct Hit
	{
		bool tracked = false;
		bool uncertainControl = false;
		std::optional<battle::VowCasualtyQuota> quota;
		battle::DamageClass damageClass = battle::DamageClass::PHYSICAL;
		int64_t count = 0, maximumHP = 0, judgment = 0;
		float weight = 1;
		PlayerColor owner = PlayerColor::CANNOT_DETERMINE;
		battle::DamageContext context();
	};
private:
	HypotheticBattle * branch;
	bool enabled = false;
	bool redemptionEnabled = false;
	bool uncertainControl = false;
	struct Pending { uint32_t actor, target; int64_t raw; std::optional<battle::VowCasualtyQuota> quota; float weight; };
	struct Observation { std::shared_ptr<StackWithBonuses> before; PlayerColor owner; int64_t loss; float weight; };
	std::vector<Pending> pending;
	std::vector<Observation> observations;
	std::map<uint32_t, battle::RedemptionEvent> redemption;
	std::map<uint32_t, consecratedArms::JudgmentPotency> potencies;
public:
	explicit DLL_LINKAGE StrikeProjection(HypotheticBattle * board);
	Hit prepare(const CBattleInfoCallback &, const BattleAttackInfo &, const battle::CUnitState &,
		int64_t amount, battle::DamageProvenance);
	void record(const BattleAttackInfo &, const battle::CUnitState &, Hit);
	/// One native makeAttack blow: all primary victims, Judgment, Redemption.
	/// No recursive strike/proc, new action, random roll or hypothetical world.
	void finish(uint32_t actor);
	bool uncertainAfflictions() const { return uncertainControl; }
	std::pair<float, float> value(DamageCache &, const std::shared_ptr<CBattleInfoCallback> &, PlayerColor) const;
};
}
