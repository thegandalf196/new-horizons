/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSylvanTactics.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include "NewHorizonsCombatSkills.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <algorithm>
namespace newHorizonsSylvanTactics
{
void captureVictim(StrikeSnapshot & snapshot, const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * target)
{
	if(snapshot.valid && attacker && ordinary(target) && battle.battleMatchOwner(attacker, target))
		snapshot.eligibleTargets.insert(target->unitId());
}
StrikeSnapshot captureStrike(const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * primary,
	const std::vector<const battle::Unit *> & targets, bool shooting, bool physical)
{
	StrikeSnapshot result;
	if(!physical || !newHorizonsCombatSkills::isPhysicalAttackDamage(attacker, shooting)
		|| !ordinary(attacker)) return result;
	const auto side = battle.playerToSide(battle.battleGetOwner(attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return result;
	const auto tactics = battle.getBattle()->getSylvanLuckState(side).tactics;
	if(!tactics.enabled) return result;
	result.valid = true;
	result.shooting = shooting;
	result.shooter = attacker->unitId();
	if(primary) result.primary = primary->unitId();
	for(const auto * target : targets) captureVictim(result, battle, attacker, target);
	if(tactics.coveringFire && shooting)
		for(uint32_t id : battle.battleFortuneAdjacentFriends(attacker))
			if(ordinary(battle.battleGetUnitByID(id))) result.adjacentFriends.push_back(id);
	return result;
}
void finishStrike(const StrikeSnapshot & snapshot,
	const std::vector<std::pair<uint32_t, int64_t>> & actualHits,
	const std::vector<uint32_t> & destroyedWithoutRebirth, SylvanTacticsState & state)
{
	if(!snapshot.valid || !state.enabled) return;
	std::vector<uint32_t> ranged;
	std::vector<uint32_t> melee;
	bool enemyKilled = false;
	for(const auto & [id, damage] : actualHits)
	{
		if(!snapshot.eligibleTargets.contains(id)) continue;
		if(std::ranges::find(destroyedWithoutRebirth, id) != destroyedWithoutRebirth.end()) enemyKilled = true;
		if(damage <= 0) continue;
		if(snapshot.shooting) ranged.push_back(id);
		else if(snapshot.primary == id) melee.push_back(id);
	}
	auto neighbors = snapshot.adjacentFriends;
	std::erase_if(neighbors, [&destroyedWithoutRebirth](uint32_t id)
	{
		return std::ranges::find(destroyedWithoutRebirth, id) != destroyedWithoutRebirth.end();
	});
	state.finishStrike(snapshot.shooter, ranged, melee, neighbors, enemyKilled);
}
void finishStrike(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * primary, const std::vector<BattleStackAttacked> & hits, bool shooting,
	bool physical, SylvanTacticsState & state)
{
	if(!state.enabled) return;
	std::vector<const battle::Unit *> targets;
	std::vector<std::pair<uint32_t, int64_t>> actualHits;
	std::vector<uint32_t> destroyed;
	for(const auto & hit : hits)
	{
		targets.push_back(battle.battleGetUnitByID(hit.stackAttacked));
		actualHits.emplace_back(hit.stackAttacked, hit.damageAmount);
		if(hit.killed() && !hit.willRebirth()) destroyed.push_back(hit.stackAttacked);
	}
	finishStrike(captureStrike(battle, attacker, primary, targets, shooting, physical), actualHits, destroyed, state);
}
void validateStrikeTransition(const CBattleInfoCallback & battle, const BattleAttack & packet)
{
	const auto * attacker = battle.battleGetUnitByID(packet.stackAttacking);
	if(!attacker) throw std::runtime_error("Sylvan Tactics strike references a missing actor");
	const auto sylvanActorSide = battle.playerToSide(battle.battleGetOwner(attacker));
	if((sylvanActorSide == BattleSide::ATTACKER || sylvanActorSide == BattleSide::DEFENDER)
		&& battle.getBattle()->getSylvanLuckState(sylvanActorSide).tactics.enabled && !packet.fortuneState)
		throw std::runtime_error("Missing captured Sylvan Tactics strike state");
	if(packet.fortuneState)
	{
		if((packet.fortuneSide != BattleSide::ATTACKER && packet.fortuneSide != BattleSide::DEFENDER)
			|| packet.fortuneSide != sylvanActorSide)
			throw std::runtime_error("Invalid Sylvan Tactics strike provider");
		auto expected = battle.getBattle()->getSylvanLuckState(packet.fortuneSide).tactics;
		const auto * primary = packet.bsa.empty() ? nullptr : battle.battleGetUnitByID(packet.bsa.front().stackAttacked);
		newHorizonsSylvanTactics::finishStrike(battle, attacker, primary, packet.bsa, packet.shot(),
			newHorizonsCombatSkills::isPhysicalAttackDamage(attacker, packet.shot(), packet.spellLike()), expected);
		if(packet.fortuneState->tactics != expected)
			throw std::runtime_error("Invalid Sylvan Tactics attack transition");
	}
}

}
