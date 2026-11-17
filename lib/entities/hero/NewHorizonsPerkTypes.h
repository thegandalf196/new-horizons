/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once

#include "../../json/JsonNode.h"
#include <string>

namespace newHorizonsHeroes
{
struct DLL_LINKAGE PerkSelection
{
	std::string skillId;
	std::string perkId;

	bool operator==(const PerkSelection &) const = default;

	template<typename Handler> void serialize(Handler & h)
	{
		h & skillId;
		h & perkId;
	}
};

struct DLL_LINKAGE PerkModifier
{
	std::string skillId;
	std::string perkId;
	int requiredRank = 0;
	bool enabled = false;
	JsonNode effect;

	bool operator==(const PerkModifier &) const = default;
};

/// Immutable, server-authored candidate snapshot used by a level-up query.
/// Identity and presentation come from the hero's saved rules, never from
/// whatever defaults happen to be installed on the replying client/server.
struct DLL_LINKAGE PerkOfferCandidate
{
	PerkSelection selection;
	std::string name;
	std::string description;
	int requiredRank = 0;

	bool operator==(const PerkOfferCandidate &) const = default;

	template<typename Handler> void serialize(Handler & h)
	{
		h & selection;
		h & name;
		h & description;
		h & requiredRank;
	}
};

}
