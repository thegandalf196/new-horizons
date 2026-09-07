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
	for(const auto & [key, category] : snapshot["creatures"].Struct())
	{
		const auto & creatures = LIBRARY->creh->objects;
		const bool canonical = std::any_of(creatures.begin(), creatures.end(), [&](const auto & creature)
		{
			return creature && creature->getJsonKey() == key;
		});
		if(!canonical)
			throw std::runtime_error("Unknown or noncanonical creature category identifier: " + key);
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
