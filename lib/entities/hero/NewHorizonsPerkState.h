/*
 * NewHorizonsPerkState.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "NewHorizonsPerkRules.h"

#include <functional>
#include <string>
#include <vector>

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

/// Hero-owned, saved generic perk identity. This layer validates selections and
/// exposes data-only projections; effect handlers live in later runtime layers.
class DLL_LINKAGE PerkState
{
public:
	JsonNode rules;
	std::vector<PerkSelection> selected;

	bool hasSelection(const std::string & skillId, const std::string & perkId) const;
	void validate() const;
	void select(const std::string & skillId, const std::string & perkId, int currentRank);
	std::vector<PerkModifier> project(const std::function<int(const std::string &)> & rankLookup) const;
	JsonNode toJson() const;
	static PerkState fromJson(const JsonNode & node);

	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validate();
		h & rules;
		h & selected;
		if(!h.saving)
			validate();
	}
};
}
