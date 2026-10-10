/*
 * NewHorizonsDiplomacy.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../json/JsonNode.h"

#include <cstdint>

namespace newHorizonsDiplomacy
{
inline constexpr const char * SKILL_ID = "new-horizons:diplomacy";
inline constexpr const char * NEGOTIATOR_ID = "new-horizons:diplomacy.negotiator";
inline constexpr const char * COMMON_CAUSE_ID = "new-horizons:diplomacy.commonCause";
inline constexpr const char * GRAND_DIPLOMAT_ID = "new-horizons:diplomacy.grandDiplomat";
inline constexpr const char * RECRUITMENT_PACT_ID = "new-horizons:diplomacy.recruitmentPact";
inline constexpr const char * LEGENDARY_REPUTATION_ID = "new-horizons:diplomacy.legendaryReputation";

/// Result shared by the neutral-creature encounter, feedback and adventure AI.
struct DLL_LINKAGE Forecast
{
	/// The captured hero ruleset declares at least one Diplomacy rank active.
	bool usesNewHorizonsRules = false;
	/// The hero has a positive New Horizons Diplomacy rank under those rules.
	bool active = false;
	bool eligible = false;
	bool willing = false;
	bool authoredFree = false;
	/// A payable qualifying offer is waived by this hero's unused calendar month.
	bool legendaryReputation = false;
	int32_t skillRank = 0;
	int32_t thresholdPercent = 0;
	bool negotiator = false;
	bool commonCause = false;
	bool grandDiplomat = false;
	/// Recruitment Pact is actually lowering this eligible stack's threshold value.
	bool recruitmentPact = false;
	uint64_t heroArmyValue = 0;
	uint64_t creatureArmyValue = 0;
	/// Full-stack ordinary recruitment price, even for an authored-free offer.
	int64_t normalGoldCost = 0;
	/// The full current neutral stack; the legacy partial-joining setting is ignored.
	int64_t joiningAmount = 0;
	bool normalGoldCostValid = true;
	/// True only if the paid offer price can safely be returned by takenAction's int API.
	bool normalGoldCostFitsAction = true;
	int64_t goldCost() const { return authoredFree || legendaryReputation ? 0 : normalGoldCost; }
};

/// Pure inputs for resolving one visible neutral stack against a captured hero profile.
struct DLL_LINKAGE ForecastInput
{
	bool usesNewHorizonsRules = false;
	bool encounterEligible = false;
	bool authoredFree = false;
	int32_t skillRank = 0;
	bool negotiator = false;
	bool commonCause = false;
	bool grandDiplomat = false;
	bool recruitmentPact = false;
	bool legendaryReputation = false;
	uint64_t heroArmyValue = 0;
	uint64_t creatureArmyValue = 0;
	int64_t goldCostPerCreature = 0;
	int64_t joiningAmount = 0;
};

/// Uses only the captured PerkState registry, never the currently installed registry.
DLL_LINKAGE bool usesNewHorizonsRules(const JsonNode & capturedPerkRules);

/// Resolve rank thresholds, exact half-value comparisons and checked full-stack Gold cost.
DLL_LINKAGE Forecast resolveForecast(const ForecastInput & input);
}
