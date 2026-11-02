/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameCaptureFacts.h"
#include "../bonuses/BonusLimitation.h"
#include "../bonuses/Limiters.h"
#include "../bonuses/BonusSelector.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../mapObjects/army/CStackInstance.h"
#include <stdexcept>
namespace battle::ownedFrameCapture
{
namespace
{
OwnedBonusEvaluationFacts::AdjacentCreature traits(const CCreature & creature, const Requirements & needed)
{
	OwnedBonusEvaluationFacts::AdjacentCreature result;
	// These trait bits are finalized from same-batch OWNED creature graphs,
	// never by evaluating a native Bonus/updater/limiter during capture.
	for(const auto family : needed.adjacentFamilies)
		result.familyUpgrades.emplace(family, family.toCreature()->isMyDirectOrIndirectUpgrade(&creature));
	return result;
}
}
OwnedUnitCreatureResolver::Creature descriptor(const CCreature & type)
{
	OwnedUnitCreatureResolver::Creature value;
	value.id = type.getId(); value.faction = type.getFactionID(); value.level = type.getLevel();
	value.doubleWide = type.isDoubleWide(); value.icon = type.getIconIndex(); value.warMachine = type.warMachine;
	value.cost = type.getFullRecruitCost(); value.key = type.getJsonKey();
	value.aiValue = type.getAIValue(); value.baseHitPoints = type.getBaseHitPoints();
	value.pluralTextID = type.getNamePluralTextID(); value.singularTextID = type.getNameSingularTextID();
	return value;
}
OwnedBonusEvaluationFacts facts(const CBonusSystemNode & node, const Requirements & needed,
	const std::vector<const CCreature *> & visibleForms, const CStack * recipient, const CCreature * effective)
{
	BonusEvaluationContext context(node);
	OwnedBonusEvaluationFacts out; out.nodeType = node.getNodeType(); out.owner = context.getOwner();
	out.heroLevel = context.heroLevel(); out.stackCount = context.stackCount(); out.stableStackLevel = context.stackLevel();
	if(const auto * army = dynamic_cast<const CArmedInstance *>(&node))
	{
		out.armySlots.emplace();
		for(const auto & [slot, value] : army->Slots())
		{
			OwnedBonusEvaluationFacts::ArmySlot input{value->getCreatureID(), value->getFactionID(), std::nullopt, value->getCount()};
			if(value->getType()) input.level = value->getLevel(); // Preserve representable null/random placeholders lazily.
			out.armySlots->push_back(std::move(input));
		}
	}
	out.creatureContext.emplace();
	const auto * creature = recipient && &node == recipient && effective ? effective : context.creature();
	if(creature)
	{
		out.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{creature->getId(), creature->getLevel(), creature->getAlignment()};
		for(const auto family : needed.families)
			out.creatureContext->familyUpgrades.emplace(family, family.toCreature()->isMyDirectOrIndirectUpgrade(creature));
	}
	out.creatureContext->faction = recipient && &node == recipient && effective ? std::optional<FactionID>(effective->getFactionID()) : context.faction();
	for(const auto source : needed.sourceCreatures) out.creatureContext->sourceFactions.emplace(source, context.sourceCreatureFaction(source));
	out.rankContext.emplace();
	if(const auto * instance = context.stackInstance())
		out.rankContext->instance = OwnedBonusEvaluationFacts::RankContext::Instance{instance->getNodeType() == BonusNodeType::COMMANDER,
			instance->getNodeType() == BonusNodeType::COMMANDER ? 0 : instance->getExpRank()};
	// Capture only evaluation destinations for which the authoritative limiter
	// can request terrain. Creature/source nodes deliberately have no such domain.
	if(needed.terrain && out.creatureContext->faction && TerrainLimiter::appliesToNode(out.nodeType))
	{
		out.terrainContext.emplace(); auto & terrain = *out.terrainContext;
		terrain.adventure = context.adventureTerrain(); terrain.query = context.queryTerrain(); terrain.battle = context.battleTerrain();
		terrain.explicitNativeContext = context.hasNativeContext();
		const auto resolved = context.resolveTerrain(terrain.adventure); terrain.legacyFallback = resolved;
		terrain.nativeMembership.emplace(resolved, context.nativeTerrainMatches(terrain.adventure));
	}
	if(recipient && &node == recipient)
	{
		if(!recipient->base && effective) out.stableStackLevel = effective->getLevel(); // Canonical baseless stack effective level.
		out.acceptance = captureOwnedBonusAcceptance(*recipient);
		// Same source-domain policy, with the recipe's explicitly admitted form.
		out.acceptance->state.currentForm = effective->getId();
		if(needed.adjacency)
		{
			out.adjacencyUnits.emplace(); out.adjacencyTraits.emplace();
			for(const auto * type : visibleForms) out.adjacencyTraits->try_emplace(type->getId(), traits(*type, needed));
		}
	}
	out.validate(); return out;
}
}
