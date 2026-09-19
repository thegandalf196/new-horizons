/*
 * NewHorizonsPerkState.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "NewHorizonsPerkRules.h"

#include <functional>
#include <cstdint>
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
	std::vector<PerkOfferCandidate> prepareOffer(
		const std::function<int(const std::string &)> & rankLookup, uint64_t seed) const;
	void acceptOffer(const std::vector<PerkOfferCandidate> & offer, size_t choice,
		const std::function<int(const std::string &)> & rankLookup, uint64_t seed);
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
