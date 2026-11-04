/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GameStatePackVisitor.h"
#include "CGameState.h"
#include "../CStack.h"
#include "../battle/BattleInfo.h"
#include "../battle/NewHorizonsDemonicGating.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/PacksForClientBattle.h"

bool GameStatePackVisitor::commitDemonicGateStartAction(const StartAction & pack, CStack & source)
{
	if(pack.ba.side != BattleSide::ATTACKER && pack.ba.side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid Demonic Gating StartAction side");
	// Mobile Gate commits only after authoritative movement and revalidation.
	if(pack.ba.target.size() == 2)
		return false;
	auto * battle = gs.getBattle(pack.battleID);
	auto & side = battle->getSide(pack.ba.side);
	const auto planned = newHorizonsDemonicGating::plan(*battle, pack.ba);
	if(!planned)
		throw std::runtime_error("Invalid Demonic Gating StartAction snapshot");
	if(planned->freeActivation)
	{
		const auto * active = battle->battleActiveUnit();
		if(!active || active != &source || active->unitSide() != pack.ba.side)
			throw std::runtime_error("Master Gate StartAction does not match the active stack");
	}
	SideInBattle::PendingDemonicGate gate;
	gate.creature = planned->creature;
	gate.count = planned->count;
	gate.position = planned->position;
	gate.arrivalRound = battle->getRound() + 1;
	gate.sourceUnitId = planned->sourceUnitId;
	gate.temporary = planned->temporary;
	// Allocation precedes the non-throwing value/erase commits.
	side.pendingDemonicGates.push_back(gate);
	if(gate.temporary)
		source.demonicGateOpened = true;
	else
		side.demonicReserve.erase(gate.creature);
	if(planned->freeActivation)
		side.masterGateUsed = true;
	return planned->freeActivation;
}

void GameStatePackVisitor::visitBattleDemonicGatingStateChanged(BattleDemonicGatingStateChanged & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle || (pack.side != BattleSide::ATTACKER && pack.side != BattleSide::DEFENDER))
		throw std::runtime_error("Invalid Demonic Gating battle state update");
	auto & side = battle->getSide(pack.side);
	std::vector<CStack *> newlyUsedSources;
	for(const auto & gate : pack.pending)
	{
		if(!gate.temporary || std::ranges::any_of(side.pendingDemonicGates, [&](const auto & prior)
			{ return prior.sourceUnitId == gate.sourceUnitId && prior.creature == gate.creature; }))
			continue;
		auto * source = battle->getStack(gate.sourceUnitId);
		if(!source || std::ranges::find(newlyUsedSources, source) != newlyUsedSources.end())
			throw std::runtime_error("Duplicate temporary Gate source in a state update");
		BattleAction action;
		action.actionType = EActionType::DEMONIC_GATING;
		action.side = pack.side;
		action.stackNumber = gate.sourceUnitId;
		action.gatingCreature = gate.creature;
		action.aimToHex(gate.position);
		const auto planned = newHorizonsDemonicGating::plan(*battle, action);
		if(!planned || !planned->temporary || planned->count != gate.count || pack.reserve != side.demonicReserve)
			throw std::runtime_error("Invalid temporary Gate commitment snapshot");
		newlyUsedSources.push_back(source);
	}
	if(pack.masterGateContinuationUnitId)
	{
		const auto unitId = *pack.masterGateContinuationUnitId;
		const auto * unit = battle->battleGetStackByID(unitId, false);
		const auto * active = battle->battleActiveUnit();
		const auto * hero = battle->battleGetFightingHero(pack.side);
		const auto newlyPending = std::ranges::find_if(pack.pending, [&](const auto & gate)
		{
			return gate.sourceUnitId == unitId
				&& std::ranges::none_of(side.pendingDemonicGates, [&](const auto & prior)
				{
					return prior.sourceUnitId == gate.sourceUnitId && prior.creature == gate.creature;
				});
		});
		const bool temporary = newlyPending != pack.pending.end() && newlyPending->temporary;
		bool commitmentValid = false;
		if(temporary)
			commitmentValid = std::ranges::find(newlyUsedSources, unit) != newlyUsedSources.end();
		else
		{
			const auto consumedReserve = newlyPending == pack.pending.end() ? side.demonicReserve.end()
				: side.demonicReserve.find(newlyPending->creature);
			const auto remainingReserve = newlyPending == pack.pending.end() ? pack.reserve.end()
				: pack.reserve.find(newlyPending->creature);
			commitmentValid = newlyPending != pack.pending.end() && consumedReserve != side.demonicReserve.end()
				&& newlyPending->count == consumedReserve->second
				&& (remainingReserve == pack.reserve.end() || remainingReserve->second < consumedReserve->second);
		}
		if(side.masterGateUsed || !pack.masterGateUsed || !unit || !unit->alive()
			|| unit->unitSide() != pack.side || !active || active->unitId() != unitId
			|| !unit->movedThisRound || !hero || !hero->hasActivePerk(
				"new-horizons:demonicGating", "new-horizons:demonicGating.masterGate") || !commitmentValid)
			throw std::runtime_error("Invalid Master Gate mobile continuation state update");
	}
	else if(pack.masterGateUsed != side.masterGateUsed)
	{
		throw std::runtime_error("Demonic Gating update changed Master Gate state without a continuation");
	}
	side.demonicReserve = std::move(pack.reserve);
	side.pendingDemonicGates = std::move(pack.pending);
	side.gatedDemonicStacks = std::move(pack.gated);
	side.chainGateArmed = pack.chainGateArmed;
	side.masterGateUsed = pack.masterGateUsed;
	for(auto * source : newlyUsedSources)
		source->demonicGateOpened = true;
	if(pack.masterGateContinuationUnitId)
		battle->getStack(*pack.masterGateContinuationUnitId)->movedThisRound = false;
}
