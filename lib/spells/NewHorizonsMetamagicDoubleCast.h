/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../Global.h"
#include <array>
#include <cstdint>
#include <optional>

class JsonNode;

namespace newHorizonsMetamagic
{
inline constexpr int64_t DOUBLE_CAST_POLICY_VERSION = 7;

/// Metamagic 6 (Double Cast): captured policy version 7. Every value is
/// authored data; the battle mechanics that consume them are not part of
/// this decoder. Historical policies 4/5/6 decode through resolutionPolicy().
struct DLL_LINKAGE DoubleCastPolicy
{
	std::array<int32_t, 4> usesPerCombatByRank{};
	/// The follow-up must be a different spell identity (a mass variant with its own ID differs).
	bool followUpMustDiffer = false;
	int32_t halonAdditionalUses = 0;
	int32_t arcaneWardSpellPowerMultiplier = 0;
	int32_t manaSiphonMana = 0;
	int32_t deepReservesAdditionalUses = 0;
	int32_t formulaReserveMana = 0;
	int32_t arcaneCycleRoundInterval = 0;
	int32_t afterglowAdditionalRounds = 0;
	int32_t crescendoSpellPowerPerSpentUse = 0;
	int32_t cleansingWeaveEffects = 0;
	int32_t grandWeavingAdditionalUses = 0;
	int32_t spellstormManaReductionPercent = 0;
	int32_t spellstormDoubleCastThreshold = 0;
};

/// Strict decoder shared by cold admission and one-time typed compilation.
/// Only a complete, bounded version 7 document is executable.
DLL_LINKAGE std::optional<DoubleCastPolicy> doubleCastPolicy(const JsonNode & rules);
/// True for any document that claims version 7, so admission never falls
/// through to the historical marks/points validation.
DLL_LINKAGE bool claimsDoubleCastPolicy(const JsonNode & rules);
/// Captured Metamagic 7 or Consecrated Arms 4 rules need format 1165 or later.
DLL_LINKAGE bool requiresDoubleCastFormat(const JsonNode & rules);
}
