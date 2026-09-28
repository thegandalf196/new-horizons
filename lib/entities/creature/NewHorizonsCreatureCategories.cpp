/*
 * NewHorizonsCreatureCategories.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsCreatureCategoryRules.h"
#include "../../GameLibrary.h"
#include "../../CCreatureHandler.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../battle/IBattleState.h"
#include <stdexcept>

const newHorizonsCreatures::CreatureCategoryRules & IGameInfoCallback::getCreatureCategoryRules() const
{
	static const newHorizonsCreatures::CreatureCategoryRules absent;
	return absent;
}

int IGameInfoCallback::getCreatureBaseGrowth(CreatureID creature) const
{
	const auto * entity = creature.toCreature();
	if(!entity)
		return 0;
	const auto configured = getCreatureCategoryRules().weeklyBaseGrowth(entity->getJsonKey());
	return configured.value_or(entity->getGrowth());
}

std::optional<int> IGameInfoCallback::getCreatureHordeGrowthOverride(CreatureID creature) const
{
	const auto * entity = creature.toCreature();
	if(!entity)
		return std::nullopt;
	return getCreatureCategoryRules().hordeGrowthOverride(entity->getJsonKey());
}

std::optional<newHorizonsCreatures::CreatureCategoryView> IGameInfoCallback::getCreatureCategory(CreatureID creature) const
{
	return newHorizonsCreatures::creatureCategoryView(getCreatureCategoryRules(), creature);
}

const newHorizonsCreatures::CreatureCategoryRules & IBattleInfo::getCreatureCategoryRules() const
{
	static const newHorizonsCreatures::CreatureCategoryRules absent;
	return absent;
}

namespace newHorizonsCreatures
{
void validateCreatureCategoryEntities(const CreatureCategoryRules & rules)
{
	const auto & snapshot = rules.getRules();
	if(snapshot.isNull() || snapshot.Struct().empty())
		return;
	if(!LIBRARY || !LIBRARY->creh)
		throw std::runtime_error("Creature categories require a loaded entity registry");
	std::map<std::string, const CCreature *, std::less<>> creaturesByKey;
	for(const auto & creature : LIBRARY->creh->objects)
		if(creature)
			creaturesByKey.emplace(creature->getJsonKey(), creature.get());

	for(const auto & [key, category] : snapshot["creatures"].Struct())
	{
		if(!creaturesByKey.contains(key))
			throw std::runtime_error("Unknown or noncanonical creature category identifier: " + key);
	}

	for(const auto & [baseKey, line] : rules.getGrowthLines())
	{
		const auto base = creaturesByKey.find(baseKey);
		if(base == creaturesByKey.end())
			throw std::runtime_error("Unknown or noncanonical creature growth-line base: " + baseKey);
		for(const auto & memberKey : line.members)
		{
			const auto member = creaturesByKey.find(memberKey);
			if(member == creaturesByKey.end())
				throw std::runtime_error("Unknown or noncanonical creature growth-line member: " + memberKey);
			if(memberKey != baseKey && !base->second->isMyDirectOrIndirectUpgrade(member->second))
				throw std::runtime_error("Creature is not an upgrade of its growth-line base: " + memberKey);
		}
	}
}

CreatureCategoryRules captureCreatureCategoryRules(const JsonNode & snapshot)
{
	CreatureCategoryRules candidate(snapshot);
	validateCreatureCategoryEntities(candidate);
	return candidate;
}

std::optional<CreatureCategoryView> creatureCategoryView(const CreatureCategoryRules & rules, CreatureID creature)
{
	const auto & snapshot = rules.getRules();
	if(snapshot.isNull() || snapshot.Struct().empty() || !LIBRARY || !LIBRARY->creh)
		return std::nullopt;
	const auto & creatures = LIBRARY->creh->objects;
	const int index = creature.getNum();
	if(index < 0 || static_cast<size_t>(index) >= creatures.size() || !creatures[index])
		return std::nullopt;
	return rules.lookup(creatures[index]->getJsonKey());
}
}
