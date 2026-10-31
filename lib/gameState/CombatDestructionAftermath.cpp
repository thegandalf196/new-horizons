/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CombatDestructionAftermath.h"
#include "../battle/BattleInfo.h"
#include "../CStack.h"
#include "../networkPacks/PacksForClientBattle.h"
namespace battleDestructionAftermath
{
std::set<uint32_t> bloodrageDeathCandidates(BattleInfo & battle, const std::vector<BattleStackAttacked> & updates)
{
	std::set<uint32_t> result;
	for(const auto & update : updates)
	{
		const auto * unit = battle.getStack(update.stackAttacked, false);
		// BattleStackAttacked::KILLED is authored while preparing the transition;
		// the stack state may already reflect that transition before this packet is
		// applied, so it is the authoritative alive-to-dead marker here.
		if(update.killed() && !update.willRebirth() && unit
			&& !unit->acquireState()->summoned && !unit->isClone())
			result.insert(update.stackAttacked);
	}
	return result;
}

std::vector<newHorizonsCrisisCommand::DeathReceipt> crisisDeathCandidates(
	BattleInfo & battle, const std::vector<BattleStackAttacked> & updates)
{
	std::vector<newHorizonsCrisisCommand::DeathReceipt> result;
	for(const auto & update : updates)
	{
		const auto * unit = battle.getStack(update.stackAttacked, false);
		// Rebirth is already restored in the prepared hit state. Clone and
		// summoned destruction still qualify. Capture control before cleanup.
		if(!unit || (!update.killed() && !update.cloneKilled()) || update.willRebirth()) continue;
		const auto side = battle.playerToSide(battle.battleGetActionController(unit));
		if((side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
			&& newHorizonsCrisisCommand::eligible(battle.getSideHero(side)))
			result.push_back({side, unit->unitId()});
	}
	return result;
}

void recordCrisisDeaths(BattleInfo & battle, const std::vector<newHorizonsCrisisCommand::DeathReceipt> & deaths)
{
	auto state = battle.getCrisisCommandState();
	for(const auto & receipt : deaths)
		if(const auto * unit = battle.getStack(receipt.unit, false); unit && !unit->alive())
			state.capture(receipt.side, receipt.unit);
	battle.setCrisisCommandState(state);
}

void recordBloodrageDeaths(BattleInfo & battle, const std::set<uint32_t> & candidates)
{
	for(const auto unitId : candidates)
		battle.recordBloodrageStackDeath(unitId);
}

void refreshBloodrageLivingUnits(BattleInfo & battle, const std::vector<BattleStackAttacked> & updates)
{
	for(const auto & update : updates)
	{
		const auto * unit = battle.getStack(update.stackAttacked, false);
		if(unit && unit->alive())
			battle.clearBloodrageStackDeath(update.stackAttacked);
	}
}

std::vector<newHorizonsReactiveDefense::Destruction> solidarityDeathCandidates(
	BattleInfo & battle, const std::vector<BattleStackAttacked> & updates)
{
	std::vector<newHorizonsReactiveDefense::Destruction> result;
	for(const auto & update : updates)
	{
		const auto * unit = battle.getStack(update.stackAttacked, false);
		if(!unit || (!update.killed() && !update.cloneKilled()) || update.willRebirth())
			continue;
		if(const auto receipt = newHorizonsReactiveDefense::captureDestruction(*unit))
			result.push_back(*receipt);
	}
	return result;
}
std::vector<newHorizonsReactiveDefense::Destruction> solidarityDeathCandidates(
	BattleInfo & battle, const std::vector<UnitChanges> & changes)
{
	std::vector<newHorizonsReactiveDefense::Destruction> result;
	for(const auto & change : changes)
	{
		const auto * unit = battle.getStack(change.id, false);
		if(change.operation != BattleChanges::EOperation::UPDATE || !unit || !unit->alive()
			|| change.healthDelta >= 0)
			continue;
		if(const auto receipt = newHorizonsReactiveDefense::captureDestruction(*unit))
			result.push_back(*receipt);
	}
	return result;
}
void recordSolidarityDeaths(BattleInfo & battle,
	std::span<const newHorizonsReactiveDefense::Destruction> deaths)
{
	std::vector<newHorizonsReactiveDefense::Destruction> destroyed;
	for(const auto & death : deaths)
		if(const auto * unit = battle.getStack(death.unit, false); unit && !unit->alive())
			destroyed.push_back(death);
	for(const auto & plan : newHorizonsReactiveDefense::solidarityAftermath(battle, destroyed))
	{
		if(!plan.remove.empty())
			battle.removeUnitBonus(plan.target, plan.remove);
		battle.addUnitBonus(plan.target, plan.add);
	}
}
}
