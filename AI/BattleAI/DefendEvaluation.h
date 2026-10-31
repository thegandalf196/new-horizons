/* New Horizons Defend evaluation; GPL v2.0 or later, see license.txt. */
#pragma once
#include <memory>

class CGHeroInstance;
class HypotheticBattle;
class StackWithBonuses;
namespace battle { class Unit; }

namespace defendEvaluation
{
/// Only the capability reads used by the existing Defend fallback.
struct DLL_LINKAGE Capabilities
{
	const CGHeroInstance * hero = nullptr;
	int bulwarkRank = 0;
	bool holdFast = false;
	bool battlefieldMastery = false;
	bool hasDefendValue = false;
	bool maintenance = false;
};
DLL_LINKAGE Capabilities capabilities(const HypotheticBattle & battle, const battle::Unit & stack);
/// The caller has established an ordinary voluntary Defend preview.
DLL_LINKAGE int projectWoundHealing(HypotheticBattle & battle, StackWithBonuses & target);
/// Bounded adjacent-threat proxy, not a new reachability or attack-sequence search.
DLL_LINKAGE float extraRetaliationValue(const HypotheticBattle & before,
	const std::shared_ptr<HypotheticBattle> & defended, const battle::Unit & original);
}
