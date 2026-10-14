/*
 * NewHorizonsPerkState.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "NewHorizonsPerkRules.h"

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
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
private:
	void migrateRetiredPerks();
	void normalizeLegacyTierConflicts();

public:
	JsonNode rules;
	std::vector<PerkSelection> selected;

	bool hasSelection(const std::string & skillId, const std::string & perkId) const;
	/// Whether ordinary advancement from currentRank to currentRank + 1 is unlocked.
	/// Exceptional external rank grants intentionally bypass this check.
	bool canAdvanceSkillNormally(std::string_view skillId, int currentRank) const;
	void validate() const;
	void select(const std::string & skillId, const std::string & perkId, int currentRank);
	/// Weight lookup reads captured hero rules. Old captured policy ignores it.
	/// Active policy requires explicit captured-table or verified legacy context.
	using OfferWeightLookup = std::function<std::optional<int>(const std::string &)>;
	enum class OfferWeightMode
	{
		Unspecified,
		CapturedTable,
		IntentionalLegacyNoCapturedTable
	};
	struct DLL_LINKAGE OfferWeightContext
	{
		OfferWeightMode mode;
		OfferWeightLookup lookup;

		OfferWeightContext() : mode(OfferWeightMode::Unspecified) {}
		OfferWeightContext(OfferWeightMode mode, OfferWeightLookup lookup)
			: mode(mode), lookup(std::move(lookup)) {}
	};
	std::vector<PerkOfferCandidate> prepareOffer(
		const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
		const OfferWeightContext & weightContext = {}) const;
	void acceptOffer(const std::vector<PerkOfferCandidate> & offer, size_t choice,
		const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
		const OfferWeightContext & weightContext = {});
	std::vector<PerkModifier> project(const std::function<int(const std::string &)> & rankLookup) const;
	JsonNode toJson() const;
	static PerkState fromJson(const JsonNode & node);

	template<typename Handler> void serialize(Handler & h)
	{
		const JsonNode & capturedRules = rules;
		if(h.saving)
			validate();
		if(h.saving)
			validateCrisisCommandProfileSerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND));
		if(h.saving && !capturedRules["offerWeightPolicyVersion"].isNull()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES))
			throw std::runtime_error("Cannot serialize weighted perk policy to an older format");
		h & rules;
		if(!h.saving && !capturedRules["offerWeightPolicyVersion"].isNull()
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES))
			throw std::runtime_error("Cannot read weighted perk policy from an older format");
		if(!h.saving)
			validateCrisisCommandProfileSerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND));
		h & selected;
		if(!h.saving)
		{
			migrateRetiredPerks();
			normalizeLegacyTierConflicts();
			validate();
		}
	}
};
}
