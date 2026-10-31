/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusEvaluationContext.h"
#include "CBonusSystemNode.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../mapObjects/army/CStackInstance.h"
#include "../entities/faction/CFaction.h"
#include "../GameLibrary.h"
#include <stdexcept>
namespace
{
struct CreatureReader
{
	const CCreature * native = nullptr;
	const OwnedBonusEvaluationFacts::CreatureContext::Creature * copied = nullptr;
	CreatureID getId() const { return native ? native->getId() : copied->id; }
	int getLevel() const { return native ? native->getLevel() : copied->level; }
	EAlignment getAlignment() const { return native ? native->getAlignment() : copied->alignment; }
};
std::optional<CreatureReader> readCreature(const BonusEvaluationContext & context, const OwnedBonusEvaluationFacts * owned)
{
	if(owned)
	{
		if(!owned->creatureContext) throw std::logic_error("Owned creature domain is unbound");
		if(!owned->creatureContext->creature) return std::nullopt;
		return CreatureReader{nullptr, &*owned->creatureContext->creature};
	}
	if(const auto * value = context.creature()) return CreatureReader{value, nullptr};
	return std::nullopt;
}
bool upgradeOf(const CreatureReader & creature, CreatureID family, const OwnedBonusEvaluationFacts * owned)
{
	if(!owned) return family.toCreature()->isMyDirectOrIndirectUpgrade(creature.native);
	const auto found = owned->creatureContext->familyUpgrades.find(family);
	if(found == owned->creatureContext->familyUpgrades.end()) throw std::logic_error("Owned family relation was not captured");
	return found->second;
}
struct RankReader
{
	const CStackInstance * native = nullptr;
	const OwnedBonusEvaluationFacts::RankContext::Instance * copied = nullptr;
	bool commander() const { return native ? native->getNodeType() == BonusNodeType::COMMANDER : copied->commander; }
	int rank() const { return native ? native->getExpRank() : copied->rank; }
};
std::optional<RankReader> readRank(const BonusEvaluationContext & context, const OwnedBonusEvaluationFacts * owned)
{
	if(owned)
	{
		if(!owned->rankContext) throw std::logic_error("Owned rank domain is unbound");
		if(!owned->rankContext->instance) return std::nullopt;
		return RankReader{nullptr, &*owned->rankContext->instance};
	}
	if(const auto * value = context.stackInstance()) return RankReader{value, nullptr};
	return std::nullopt;
}
}
std::optional<bool> BonusEvaluationContext::matchesCreature(CreatureID family, bool includeUpgrades) const
{
	const auto c = readCreature(*this, owned);
	if(!c) return std::nullopt;
	return c->getId() == family || (includeUpgrades && upgradeOf(*c, family, owned));
}
std::optional<bool> BonusEvaluationContext::matchesCreatureLevel(uint32_t minimum, uint32_t maximum) const
{
	const auto c = readCreature(*this, owned);
	if(!c) return std::nullopt;
	return c->getLevel() < maximum && c->getLevel() >= minimum;
}
std::optional<bool> BonusEvaluationContext::matchesCreatureAlignment(EAlignment alignment) const
{
	const auto c = readCreature(*this, owned);
	if(!c) return std::nullopt;
	return alignment == c->getAlignment();
}
FactionID BonusEvaluationContext::sourceCreatureFaction(CreatureID source) const
{
	if(owned)
	{
		if(!owned->creatureContext) throw std::logic_error("Owned source faction domain is unbound");
		const auto found = owned->creatureContext->sourceFactions.find(source);
		if(found == owned->creatureContext->sourceFactions.end()) throw std::logic_error("Owned source faction was not captured");
		return found->second;
	}
	return source.toCreature()->getFactionID();
}
std::optional<bool> BonusEvaluationContext::matchesRank(uint8_t minimum, uint8_t maximum) const
{
	const auto stack = readRank(*this, owned);
	if(!stack) return std::nullopt;
	if(stack->commander()) return false;
	return stack->rank() > minimum && stack->rank() < maximum;
}
std::optional<TerrainId> BonusEvaluationContext::adventureTerrain() const
{
	if(owned)
	{
		if(!owned->terrainContext) throw std::logic_error("Owned terrain domain is unbound");
		return owned->terrainContext->adventure;
	}
	if(getNodeType() == BonusNodeType::STACK_BATTLE || getNodeType() == BonusNodeType::STACK_INSTANCE)
	{
		if(const auto * stack = stackInstance()) return stack->getCurrentTerrain();
	}
	else if(const auto * army = dynamic_cast<const CArmedInstance *>(&original)) return army->getCurrentTerrain();
	return std::nullopt;
}
TerrainId BonusEvaluationContext::resolveTerrain(std::optional<TerrainId> adventure) const
{
	if(adventure) return *adventure;
	if(const auto query = queryTerrain(); query && query->hasValue()) return *query;
	if(hasNativeContext())
	{
		const auto projectedTerrain = battleTerrain();
		if(!projectedTerrain) throw std::logic_error("Terrain limiter requires an adventure or bound battle terrain");
		return *projectedTerrain;
	}
	if(owned)
	{
		if(!owned->terrainContext || !owned->terrainContext->legacyFallback)
			throw std::logic_error("Terrain limiter requires an adventure or bound battle terrain");
		return *owned->terrainContext->legacyFallback;
	}
	const auto * stack = dynamic_cast<const CStack *>(&original);
	if(!stack || !stack->getBattle()) throw std::logic_error("Terrain limiter requires an adventure or bound battle terrain");
	return stack->getCurrentTerrain();
}
bool BonusEvaluationContext::nativeTerrainMatches(std::optional<TerrainId> adventure) const
{
	if(owned)
	{
		if(!owned->terrainContext) throw std::logic_error("Owned native terrain domain is unbound");
		const auto terrain = resolveTerrain(adventure);
		const auto found = owned->terrainContext->nativeMembership.find(terrain);
		if(found == owned->terrainContext->nativeMembership.end()) throw std::logic_error("Owned native terrain membership was not captured");
		return found->second;
	}
	// Keep faction/entity resolution before terrain evaluation, as in the original member call.
	return faction()->toEntity(LIBRARY)->isNativeTerrain(resolveTerrain(adventure));
}
