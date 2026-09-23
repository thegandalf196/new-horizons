/*
 * RandomArtifactPool.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../GameLibrary.h"
#include "../../constants/EntityIdentifiers.h"
#include "CArtHandler.h"
#include "../../json/JsonNode.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"

#include <set>
#include <stdexcept>

namespace artifactRandomPool
{

inline std::set<ArtifactID> exclusionsFromSetting(const JsonNode & setting)
{
	if(setting.isNull())
		return {};

	if(!setting.isVector())
		throw std::runtime_error("Invalid random artifact pool exclusions: array required");
	if(setting.Vector().empty())
		return {};

	if(!LIBRARY || !LIBRARY->identifiers())
		throw std::runtime_error("Random artifact pool exclusions require a loaded entity registry");

	std::set<ArtifactID> result;
	for(const auto & entry : setting.Vector())
	{
		if(!entry.isString() || entry.String().empty())
			throw std::runtime_error("Invalid random artifact pool exclusion: artifact identifier required");

		const auto identifier = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), ArtifactID::entityType(), entry.String(), true);
		if(!identifier || *identifier < 0)
			throw std::runtime_error("Unknown random artifact pool exclusion: " + entry.String());

		if(!result.insert(ArtifactID(*identifier)).second)
			throw std::runtime_error("Duplicate random artifact pool exclusion: " + entry.String());
	}
	return result;
}

inline void validateExclusions(const std::set<ArtifactID> & exclusions)
{
	if(exclusions.empty())
		return;
	if(!LIBRARY || !LIBRARY->arth)
		throw std::runtime_error("Random artifact pool exclusions require a loaded artifact registry");

	for(const auto & exclusion : exclusions)
	{
		const auto index = exclusion.getNum();
		const auto & artifacts = LIBRARY->arth->objects;
		if(index < 0 || static_cast<size_t>(index) >= artifacts.size() || !artifacts[index]
			|| artifacts[index]->getId() != exclusion)
			throw std::runtime_error("Save contains an unknown random artifact pool exclusion");
	}
}

inline void removeExclusions(std::set<ArtifactID> & candidates, const std::set<ArtifactID> & exclusions)
{
	for(const auto & exclusion : exclusions)
		candidates.erase(exclusion);
}

}
