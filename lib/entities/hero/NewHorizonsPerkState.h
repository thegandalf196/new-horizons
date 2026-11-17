/*
 * NewHorizonsPerkState.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "NewHorizonsPerkRules.h"
#include "NewHorizonsPerkTypes.h"
#include "../../nativeRules/PreparedPilotRules.h"
#include "../../nativeRules/CapturedRuleRef.h"
#include "../../nativeRules/CapturedRuleTargets.h"
#include "CompiledPerkCatalogue.h"
#include "../../battle/NewHorizonsCommandResources.h"
#include "NewHorizonsCommandAdmission.h"
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
class PerkSelectionOperation;
/// Hero-owned, saved generic perk identity. This layer validates selections and
/// exposes data-only projections; effect handlers live in later runtime layers.
class DLL_LINKAGE PerkState
{
private:
	void migrateRetiredPerks(JsonNode & decodedRules);
	static void normalizeLegacyTierConflicts(const JsonNode & decodedRules, std::vector<PerkSelection> & selections);
	void validateSelections() const;
	void validateSelections(const std::vector<PerkSelection> & selections) const;
	static bool canAcquirePerkInState(const JsonNode & rules, const std::vector<PerkSelection> & selections,
		const std::string & skill, const std::string & perk, int rank);
	friend class PerkSelectionOperation;

	std::shared_ptr<const nativeRules::CapturedPerkRules> capturedRules;
	std::vector<PerkSelection> selected;
	std::vector<uint64_t> acquiredPerks;
	void rebuildProjection();
	void validateSerializationTarget(const std::function<bool(ESerializationVersion)> & supported) const;
	std::vector<uint64_t> projectSelections(const std::vector<PerkSelection> & selections) const;

public:
	const JsonNode & getRules() const;
	const std::vector<PerkSelection> & getSelected() const { return selected; }
	void replaceRules(JsonNode rules);
	void replaceSelected(std::vector<PerkSelection> selections);
	void bindCapturedRules(std::shared_ptr<const nativeRules::CapturedPerkRules> rules);
	const std::shared_ptr<const nativeRules::CapturedPerkRules> & capturedOwner() const { return capturedRules; }
	const CompiledPerkCatalogue * compiledCatalogue() const;
	bool hasSelection(const PerkId & id) const;
	const CompiledPerkDefinition * selectedDefinition(const PerkId & id) const;

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
	/// Gathers all active, unselected perks for a given skill and specific rank tier.
	std::vector<PerkOfferCandidate> candidatesForSkillTier(const std::string & skillId, int tier) const;
	/// First unresolved mandatory tier, ordered by captured Skill identity then rank.
	std::vector<PerkOfferCandidate> nextRequiredPerkOffer(const std::function<int(const std::string &)> & rankLookup) const;
	bool hasPerkForTier(const std::string & skillId, int tier) const;
	/// Initialization-only completion for genuinely non-interactive heroes.
	void selectMissingPerksDeterministically(const std::function<int(const std::string &)> & rankLookup);
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
		const JsonNode & rules = getRules();
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SHARED_CAPTURED_RULES))
		{
			if(h.saving && !nativeRules::supportsCapturedRuleTargets(h))
				validateSerializationTarget([&h](auto feature) { return h.hasFeature(feature); });
			nativeRules::CapturedRuleRef<nativeRules::CapturedRuleKind::Perks> component;
			if(h.saving)
			{
				validateSelections();
				component.bind(nativeRules::CapturedRuleDocument::wrapPerks(capturedRules
					? capturedRules : nativeRules::CapturedPerkRules::compile(JsonNode())));
			}
			h & component;
			if(h.saving)
				h & selected;
			else
			{
				PerkState candidate;
				candidate.bindCapturedRules(component.owner()->perks());
				std::vector<PerkSelection> decoded;
				h & decoded;
				candidate.normalizeLegacyTierConflicts(candidate.getRules(), decoded);
				candidate.replaceSelected(std::move(decoded));
				if(!nativeRules::supportsCapturedRuleTargets(h))
					candidate.validateSerializationTarget([&h](auto feature) { return h.hasFeature(feature); });
				if(prepared) *prepared = candidate.prepareNativeRules();
				*this = std::move(candidate);
			}
			return;
		}
		if(h.saving) validateSerializationTarget([&h](auto feature) { return h.hasFeature(feature); });
		const JsonNode & capturedRules = rules;
		JsonNode decodedRules;
		if(h.saving) h & rules;
		else h & decodedRules;
		const auto & incomingRules = h.saving ? rules : decodedRules;
		validateFourOfferProgressionSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_FOUR_OFFER_PROGRESSION));
		validateUnifiedSkillProgressionSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
		newHorizonsNecromancy::validateLeadershipPolicySerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
		validateCommandPerkSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_COMMAND_AUTHORITY),
			h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY),
			h.hasFeature(Handler::Version::NEW_HORIZONS_FOUR_OFFER_PROGRESSION), h.hasFeature(Handler::Version::NEW_HORIZONS_ESTATES));
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
			h & candidate.selected;
			candidate.migrateRetiredPerks(decodedRules);
			candidate.normalizeLegacyTierConflicts(decodedRules, candidate.selected);
			candidate.replaceRules(std::move(decodedRules));
			const auto incomingPrepared = prepared ? candidate.prepareNativeRules() : nullptr;
			if(!prepared) candidate.validate();
			*this = std::move(candidate);
			if(prepared) *prepared = incomingPrepared;
		}
	}
};
}
