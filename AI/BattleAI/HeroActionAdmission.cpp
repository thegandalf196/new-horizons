/*
 * HeroActionAdmission.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionAdmission.h"
#include "PossibleSpellcast.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/bonuses/BonusGraphQuery.h"
#include "../../lib/callback/GameCallbackHolder.h"
#include "../../lib/bonuses/OwnedBonusPayloads.h"
#include "CanonicalOrderValue.h"
#include "../../lib/spells/CSpell.h"

namespace heroActionProjection
{
Admission admitCandidate(HypotheticBattle & state, BattleSide side, const PossibleSpellcast & candidate)
{
	Admission result;
	if(candidate.command == HeroCommand::NONE)
	{
		result.spellAllowance = state.prepareHeroSpellAllowance(side, candidate.spell->getId(),
			candidate.metamagicFollowup, candidate.metamagicGrand);
		if(!result.spellAllowance)
			return result;
		if(!state.beginProjectedHeroAction(side, *result.spellAllowance))
			return result;
		result.counterspell = state.resolveProjectedCounterspell(side, candidate.spell);
		if(!result.counterspell.resolutionKnown || !result.counterspell.negated.has_value())
		{
			// The armed ward is public, but the opposing hero's mana and
			// Countermage perk may be hidden from this player's callback.
			// Do not invent either a successful or failed Counterspell result.
			return result;
		}
		result.counterspellNegated = *result.counterspell.negated;
	}
	else
	{
		result.orderAllowance = state.prepareHeroOrderAllowance(side);
		if(!result.orderAllowance)
			return result;
		result.legacyOrder = !heroCommands::isCanonicalRules(state.getHeroCommandRules());
		if(result.legacyOrder)
		{
			if(!state.beginProjectedLegacyHeroOrder(side, candidate.command,
				candidate.commandTargets, *result.orderAllowance))
				return result;
		}
		else
		{
			// Canonical snapshots remain bound before StartAction cleanup.
			result.orderEffects = state.beginProjectedHeroOrder(side, candidate.command,
				candidate.commandTargets, *result.orderAllowance);
			if(!result.orderEffects)
				return result;
		}
	}
	result.admitted = true;
	return result;
}

OrderCompletion completeOrder(HypotheticBattle & state, const CBattleInfoCallback & before,
	BattleSide side, const PossibleSpellcast & candidate, const Admission & admission)
{
	OrderCompletion result;
	if(admission.legacyOrder)
	{
		if(!state.projectAcceptedHeroOrder(side, candidate.command,
			candidate.commandTargets, *admission.orderAllowance))
			return result;
	}
	else
	{
		// Install the native pre-action snapshot, not a second locally
		// reconstructed bonus set or an aggregate raw-HP utility prior.
		if(!admission.orderEffects || !state.projectAcceptedHeroOrder(
			side, *admission.orderEffects, *admission.orderAllowance))
			return result;
		if(!heroOrderValue::usesRevisedArmyHeuristic(before, candidate.command))
			result.continuation = state.projectOrderContinuation(side,
				*admission.orderAllowance, *admission.orderEffects);
	}
	result.accepted = true;
	return result;
}

}

namespace
{
bool canonicalDamageSourceNode(const CBonusSystemNode & node, const BattleInfo & world)
{
	const auto & type = typeid(node);
	if(const auto * holder = dynamic_cast<const GameCallbackHolder *>(&node))
	 if(holder->cb != world.cb)
	  return false;
	if(const auto * native = dynamic_cast<const CStack *>(&node))
		if(!native->usesStateEnvironment(*native) || native->hasBattleFormState()
			|| (native->getBattle() && typeid(*native->getBattle()) != typeid(BattleInfo))
			|| (native->base && typeid(*native->base) != typeid(CStackInstance)))
			return false;
	if(const auto * army = dynamic_cast<const CArmedInstance *>(&node))
		for(const auto & [slot, stack] : army->Slots())
			if(!stack || typeid(*stack) != typeid(CStackInstance)
				|| !stack->getCreature() || typeid(*stack->getCreature()) != typeid(CCreature))
				return false;
	return type == typeid(CBonusSystemNode) || type == typeid(CStack)
		|| type == typeid(CStackInstance) || type == typeid(CGHeroInstance)
		|| type == typeid(CArmedInstance) || type == typeid(CGTownInstance)
		|| type == typeid(CCreature) || type == typeid(CArtifact) || type == typeid(CArtifactInstance)
		|| type == typeid(PlayerState) || type == typeid(TeamState) || type == typeid(BattleInfo);
}

bool admitNativeDamageSources(const HypotheticBattle & branch, const BattleInfo * & admittedWorld)
{
	if(!branch.hasCanonicalDamageQuerySources())
		return false;
	std::vector<std::shared_ptr<const Bonus>> records;
	std::vector<std::pair<const CBonusSystemNode *, bool>> work;
	const auto addRecord = [&](const std::shared_ptr<const Bonus> & record)
	{
		if(!record || records.size() >= 8192)
			return false;
		records.push_back(record);
		return true;
	};
	const auto addNative = [&](const CStack * native)
	{
		if(!native || typeid(*native) != typeid(CStack))
			return false;
		if(native->getBattle())
		{
			if(!branch.hasCanonicalDamageQuerySources(512, native->getBattle()))
				return false;
			admittedWorld = native->getBattle();
		}
		work.emplace_back(native, false);
		return true;
	};
	const auto units = branch.battleGetUnitsIf([](const battle::Unit *) { return true; });
	if(units.size() > 256)
		return false;
	for(const auto * unit : units)
	{
		if(!unit)
			return false;
		if(typeid(*unit) == typeid(CStack))
		{
			if(!addNative(dynamic_cast<const CStack *>(unit)))
				return false;
			continue;
		}
		const auto * projected = dynamic_cast<const StackWithBonuses *>(unit);
		if(!projected || !projected->hasCanonicalDamageQuerySources(branch))
			return false;
		std::vector<recipientBonusOverlay::Layer> layers;
		const CStack * native = nullptr;
		if(!projected->captureRecipientOverlayLayers(layers, native) || !addNative(native))
			return false;
		for(const auto & layer : layers)
		{
			for(const auto * list : {&layer.added, &layer.updated})
				for(const auto & record : *list)
					if(!addRecord(record))
						return false;
			for(const auto & record : layer.removed)
				if(!addRecord(record))
					return false;
			if(layer.stackedSnapshot)
				for(const auto & record : *layer.stackedSnapshot)
					if(!addRecord(std::make_shared<Bonus>(record)))
						return false;
			if(layer.unstackedSnapshot)
				for(const auto & record : *layer.unstackedSnapshot)
					if(!addRecord(record))
						return false;
		}
	}
	if(!admittedWorld)
	 return false;
	// Include inactive limited records. Effective lists cannot prove absence of
	// custom getter code hidden behind a currently false native limiter.
	std::set<const CBonusSystemNode *> active, complete;
	size_t edges = 0;
	const auto & graph = liveBonusGraph();
	while(!work.empty())
	{
		const auto [node, leaving] = work.back();
		work.pop_back();
		if(!node)
			return false;
		if(leaving)
		{
			active.erase(node);
			complete.insert(node);
			continue;
		}
		if(complete.contains(node))
			continue;
		if(!canonicalDamageSourceNode(*node, *admittedWorld) || !active.insert(node).second || active.size() + complete.size() > 1024)
			return false;
		for(const auto & record : graph.local(*node))
			if(!addRecord(record))
				return false;
		const auto & parents = graph.parents(*node);
		if(parents.size() > 4096 - edges)
			return false;
		edges += parents.size();
		work.emplace_back(node, true);
		for(auto it = parents.rbegin(); it != parents.rend(); ++it)
			work.emplace_back(*it, false);
	}
	ownedBonusPayloads::validateTypesAndTopology(records);
	return admittedWorld != nullptr;
}
}


bool heroActionProjection::CanonicalSourceAdmission::admits(const battle::Unit & target)
{
	const auto * branch = dynamic_cast<const HypotheticBattle *>(&battle);
	if((typeid(actor) != typeid(CStack) && typeid(actor) != typeid(StackWithBonuses))
		|| (typeid(target) != typeid(CStack) && typeid(target) != typeid(StackWithBonuses)))
		return false;
	if(!branch || !branch->hasCanonicalDamageQuerySources(512, nativeWorld))
		return false;
	// The exact pair must belong to this callback's current roster. Equal IDs,
	// detached copies and an intercepted foreign target do not transfer proof.
	if(branch->battleGetUnitByID(actor.unitId()) != &actor
		|| branch->battleGetUnitByID(target.unitId()) != &target)
		return false;
	if(!admitted.has_value())
		admitted = [&]
	{
		try
		{
			const BattleInfo * world = nullptr;
			if(!admitNativeDamageSources(*branch, world))
				return false;
			nativeWorld = world;
			return true;
		}
		catch(const std::exception &)
		{
			// Failed optional source/type admission never changes the ordinary query path.
			return false;
		}
	}();
	return *admitted;
}
