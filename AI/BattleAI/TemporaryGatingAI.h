/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/NewHorizonsDemonicGating.h"
#include <functional>
#include <optional>
#include <vector>
namespace temporaryGatingAI
{
inline constexpr size_t DEPLOYMENT_LIMIT = 8;
struct Candidate
{
	BattleAction action;
	newHorizonsDemonicGating::GatePlan plan;
	uint32_t threatUnitId = 0;
};
struct Workload
{
	size_t visibleUnits = 0;
	size_t geometryHexes = 0;
	size_t nativePlans = 0;
	size_t scoredPlans = 0;
	size_t postMovementPlans = 0;
	size_t movementPreviews = 0;
	size_t arrivingReachability = 0;
	size_t arrivingAttacks = 0;
};
/// Temporary Gating never moves its source: one visible army pass, one
/// deployment geometry pass and at most eight native legality probes.
/// Stable ties use lower hex ID; historical reserve Gating has its own policy.
std::vector<Candidate> candidates(const CBattleInfoCallback & battle,
	const battle::Unit & source, BattleSide side, Workload * workload = nullptr);
/// Provider returns signed marginal exchange utility against the same ordinary
/// activation reference. It must include actual arrival timing and spent source
/// activation; Master Gate must not pay that cost. No count x creatureAIValue.
using MarginalValue = std::function<std::optional<float>(const Candidate &)>;
/// Unsupported candidate valuation aborts rather than treating it as zero.
/// No positive candidate means retain the already selected ordinary action.
std::optional<BattleAction> choose(const std::vector<Candidate> & candidates,
	const MarginalValue & value, bool & complete, Workload * workload = nullptr);
}
