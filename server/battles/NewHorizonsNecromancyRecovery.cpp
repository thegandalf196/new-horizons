/*
 * NewHorizonsNecromancyRecovery.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRaisedArmy.h"
#include "../../lib/CStack.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

#include <algorithm>
#include <limits>

namespace newHorizonsNecromancy
{
namespace
{
int32_t restoreForm(CGameHandler & handler, const CGHeroInstance & hero,
	CreatureID form, int32_t eligible, int32_t losses, int percent)
{
	const int64_t bounded = std::max(0, std::min(eligible, losses));
	const auto requested = static_cast<int32_t>(bounded * percent / 100);
	// Existing whole-output admission is monotone in count. Find only what fits
	// the current army, preserving Leadership and the normal validated requests.
	int32_t low = 0;
	int32_t high = requested;
	while(low < high)
	{
		const auto middle = low + (static_cast<int64_t>(high) - low + 1) / 2;
		if(planRaisedArmy(hero, {{form, middle}}))
			low = static_cast<int32_t>(middle);
		else
			high = static_cast<int32_t>(middle - 1);
	}
	if(low > 0)
	{
		const auto plan = planRaisedArmy(hero, {{form, low}});
		if(!plan)
			throw std::runtime_error("Necromancy recovery admission changed without a simulation yield");
		applyRaisedArmy(handler, hero, *plan);
	}
	return low;
}
}

void captureOwnSkeletonCasualties(const CStack & stack, const battle::CUnitState & state,
	const CGHeroInstance & hero, int32_t eligibleCasualties, BattleResult & result)
{
	if(eligibleCasualties <= 0
		|| (result.winner != BattleSide::ATTACKER && result.winner != BattleSide::DEFENDER)
		|| stack.unitSide() != result.winner || !isSkeletonFamily(state.creatureId())
		|| !state.unitSlot().validSlot() || !stack.base
		|| hero.getStackPtr(state.unitSlot()) != stack.base || stack.summoned || stack.isClone())
		return;
	// Run the same temporary-resurrection cleanup as strategic accounting on a
	// detached copy, including capacity-health cohorts. Never mutate the battle.
	auto retainedState = state.acquireState();
	retainedState->health.takeResurrected();
	const int64_t retained = retainedState->alive() ? retainedState->getCount() : 0;
	const auto lost = std::clamp<int64_t>(stack.base->getCount() - retained, 0, eligibleCasualties);
	auto & captured = result.necromancyUndeadEligibleCasualties[result.winner][state.creatureId()];
	captured = static_cast<si32>(std::min<int64_t>(std::numeric_limits<si32>::max(), captured + lost));
}

void applyBattleSkeletonRecovery(CGameHandler & handler, const BattleResult & result,
	const CGHeroInstance & hero, NecromancyResult & summary)
{
	const auto percent = graveKnowledgeRecoveryPercent(&hero);
	if(percent <= 0 || !summary.active || !result.necromancySpecialEligibilityCaptured
		|| (result.winner != BattleSide::ATTACKER && result.winner != BattleSide::DEFENDER))
		return;
	if(!validRaisedArmyState(hero))
		throw std::runtime_error("Invalid receiving army state during Skeleton recovery");
	if(summary.recoveredSkeletons != 0 || summary.recoveredSkeletonWarriors != 0)
		throw std::runtime_error("Repeated Necromancy own-loss recovery");
	const auto & eligible = result.necromancyUndeadEligibleCasualties[result.winner];
	const auto & casualties = result.casualties[result.winner];
	const auto restore = [&](CreatureID form)
	{
		const auto found = eligible.find(form);
		const auto lost = casualties.find(form);
		return found != eligible.end() && lost != casualties.end()
			? restoreForm(handler, hero, form, found->second, lost->second, percent) : 0;
	};
	// Capacity contested by both forms follows this fixed order. Restore as much
	// as fits; missed recoveries do not spill into the fresh-raising Ossuary path.
	summary.recoveredSkeletons = restore(CreatureID(CreatureID::decode("core:skeleton")));
	summary.recoveredSkeletonWarriors = restore(CreatureID(CreatureID::decode("core:skeletonWarrior")));
}
}
