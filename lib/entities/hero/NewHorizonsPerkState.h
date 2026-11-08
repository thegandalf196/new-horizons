/*
 * NewHorizonsPerkState.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "NewHorizonsPerkRules.h"
#include "../../nativeRules/PreparedPilotRules.h"
#include "../../battle/NewHorizonsCommandResources.h"
#include "../../battle/NewHorizonsElementalAttunement.h"
#include "NewHorizonsNecromancyPolicy.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <cstdint>
#include <string>
#include <vector>

namespace newHorizonsHeroes
{
struct R2WeightCache;
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
	void validateSelections() const;

public:
	JsonNode rules;
	std::vector<PerkSelection> selected;

	bool hasSelection(const std::string & skillId, const std::string & perkId) const;
	/// Whether ordinary advancement from currentRank to currentRank + 1 is unlocked.
	/// Exceptional external rank grants intentionally bypass this check.
	bool canAdvanceSkillNormally(std::string_view skillId, int currentRank) const;
	/// Shared captured-policy admission; exceptional Skill ranks do not skip perk tiers.
	bool canAcquirePerk(const std::string & skillId, const std::string & perkId, int currentRank) const;
	void validate() const;
	std::shared_ptr<const nativeRules::PreparedPilotPerks> prepareNativeRules() const;
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
		/// Complete captured class row, including unlearned/expert/capped Skills.
		std::map<std::string, int> referenceWeights;
		/// Transaction-local numerical parameters only, never an eligibility cache.
		mutable std::shared_ptr<const R2WeightCache> normalizedWeights;

		OfferWeightContext() : mode(OfferWeightMode::Unspecified) {}
		OfferWeightContext(OfferWeightMode mode, OfferWeightLookup lookup,
			std::map<std::string, int> referenceWeights = {})
			: mode(mode), lookup(std::move(lookup)), referenceWeights(std::move(referenceWeights)) {}
	};
	std::vector<PerkOfferCandidate> prepareOffer(
		const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
		const OfferWeightContext & weightContext = {}) const;
	/// Exact decimal integer weights used by the R2 sampler, common2^32 scale.
	/// Read-only numerical projection; never samples or examines eligibility.
	std::map<std::string, std::string> quantizedParentWeights(const OfferWeightContext & context) const;
	void acceptOffer(const std::vector<PerkOfferCandidate> & offer, size_t choice,
		const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
		const OfferWeightContext & weightContext = {});
	std::vector<PerkModifier> project(const std::function<int(const std::string &)> & rankLookup) const;
	JsonNode toJson() const;
	static PerkState fromJson(const JsonNode & node);

	template<typename Handler> void serialize(Handler & h, std::shared_ptr<const nativeRules::PreparedPilotPerks> * prepared = nullptr)
	{
		if(h.saving)
		{
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN))
				nativeRules::validatePilotPerks(rules, false);
			newHorizonsCommand::validateRulesSerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
			newHorizonsElementalAttunement::validatePolicySerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
		}
		if(h.saving) newHorizonsHeroes::validateSylvanTacticsSerialization(rules,
			h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_TACTICS));
		const JsonNode & capturedRules = rules;
		if(h.saving)
			validate();
		if(h.saving)
			validateCrisisCommandProfileSerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND));
		if(h.saving)
			validateWeightedPerkSerialization(capturedRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES),
				h.hasFeature(Handler::Version::NEW_HORIZONS_R2_PERK_OFFERS),
				h.hasFeature(Handler::Version::NEW_HORIZONS_STRICT_PERK_PROGRESSION),
				h.hasFeature(Handler::Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY),
				h.hasFeature(Handler::Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS));
		if(h.saving)
			newHorizonsNecromancy::validatePolicySerialization(capturedRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_SKELETON_NECROMANCY),
				h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_POLICY_V2));
		JsonNode decodedRules;
		if(h.saving) h & rules;
		else h & decodedRules;
		const auto & incomingRules = h.saving ? rules : decodedRules;
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN))
			nativeRules::validatePilotPerks(incomingRules, false);
		newHorizonsCommand::validateRulesSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
		newHorizonsElementalAttunement::validatePolicySerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
		validateSylvanTacticsSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_TACTICS));
		if(!h.saving)
			newHorizonsNecromancy::validatePolicySerialization(incomingRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_SKELETON_NECROMANCY),
				h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_POLICY_V2));
		if(!h.saving)
			validateWeightedPerkSerialization(incomingRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES),
				h.hasFeature(Handler::Version::NEW_HORIZONS_R2_PERK_OFFERS),
				h.hasFeature(Handler::Version::NEW_HORIZONS_STRICT_PERK_PROGRESSION),
				h.hasFeature(Handler::Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY),
				h.hasFeature(Handler::Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS));
		if(!h.saving)
			validateCrisisCommandProfileSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND));
		if(h.saving)
			h & selected;
		else
		{
			PerkState candidate;
			candidate.rules = std::move(decodedRules);
			h & candidate.selected;
			candidate.migrateRetiredPerks();
			candidate.normalizeLegacyTierConflicts();
			const auto incomingPrepared = prepared ? candidate.prepareNativeRules() : nullptr;
			if(!prepared) candidate.validate();
			*this = std::move(candidate);
			if(prepared) *prepared = incomingPrepared;
		}
	}
};
}
