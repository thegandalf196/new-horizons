/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleCreatureKillRewards.h"
#include <set>
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/battle/NewHorizonsSoulHarvest.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace battleCreatureKillRewards
{
void recordPredictedEnemyDestruction(const CBattleInfoCallback & battle, const battle::Unit * actor,
	const std::vector<BattleStackAttacked> & hits, bool * destroyedEnemyOut)
{
	if(!destroyedEnemyOut)
		return;
	for(const auto & hit : hits)
	{
		const auto * victim = battle.battleGetUnitByID(hit.stackAttacked);
		if(victim && hit.killed() && !hit.willRebirth() && battle.battleMatchOwner(actor, victim))
		{
			*destroyedEnemyOut = true;
			return;
		}
	}
}

void finishStrike(const CBattleInfoCallback & battle, battle::CUnitState & actor,
	const std::vector<BattleStackAttacked> & hits, bool ranged, bool counter, bool physical)
{
	std::set<uint32_t> creditedVictims;
	int64_t killedCreatures = 0;
	for(const auto & hit : hits)
	{
		if(hit.attackerID != actor.unitId() || hit.killedAmount == 0 || hit.cloneKilled() || hit.willRebirth())
			continue;
		const auto * victim = battle.battleGetUnitByID(hit.stackAttacked);
		if(!victim || !newHorizonsSoulHarvest::qualifies(battle, actor, *victim, physical))
			continue;
		// Deduplicate this blow only; later casualties after resurrection count again.
		if(!creditedVictims.insert(hit.stackAttacked).second)
			continue;
		auto state = victim->acquireState();
		state->load(hit.newState.data);
		// The live unit is still the pre-packet state. Replayed completion after
		// application has zero remaining count loss and cannot credit the hit twice.
		killedCreatures += std::min<int64_t>(hit.killedAmount,
			newHorizonsSoulHarvest::casualtiesAfterHit(*state, victim->getCount(), false));
	}
	newHorizonsSoulHarvest::grant(actor, killedCreatures);
	actor.afterAttack(ranged, counter, physical);
}
}

bool battleCreatureKillRewards::chainGateKillQualifies(const CBattleInfoCallback & battle,
	const CStack * attacker, const std::vector<BattleStackAttacked> & hits)
{
	if(!attacker)
		return false;
	const auto * concrete = dynamic_cast<const BattleInfo *>(battle.getBattle());
	if(!concrete)
		return false;
	const auto rewardSide = concrete->gatedDemonicStackSide(attacker->unitId());
	if((rewardSide != BattleSide::ATTACKER && rewardSide != BattleSide::DEFENDER)
		|| attacker->unitId() == std::numeric_limits<uint32_t>::max())
		return false;
	const auto * hero = battle.battleGetFightingHero(rewardSide);
	if(!hero || !hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.chainGate"))
		return false;

	return std::ranges::any_of(hits, [&battle, attacker, rewardSide](const auto & hit)
	{
		if(hit.attackerID != attacker->unitId() || !hit.killed() || hit.cloneKilled() || hit.willRebirth())
			return false;
		const auto * victim = battle.battleGetUnitByID(hit.stackAttacked);
		return victim && victim != attacker && victim->unitSide() != rewardSide;
	});
}
