/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <cstdint>
#include <optional>
class JsonNode;
class CHero;
class CGHeroInstance;
class PlayerState;
namespace newHorizonsNecromancy { struct NecromancyResult; struct Policy; }
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
struct DLL_LINKAGE ReclamationSpecialtyRules
{
	int conversionPercentPerLevel = 0;
	int conversionMaximumPercent = 0;
};
DLL_LINKAGE void validateReclamationSpecialtySerialization(const JsonNode & development, bool supported);
DLL_LINKAGE std::optional<ReclamationSpecialtyRules> reclamationSpecialtyRules(const JsonNode & development);
DLL_LINKAGE bool ownsZombieDwelling(const PlayerState * ownerState, const CGHeroInstance & hero);
DLL_LINKAGE int reclaimerConversionPercent(const CGHeroInstance & hero, const PlayerState * ownerState);
DLL_LINKAGE bool resolveReclaimerPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
namespace newHorizonsNecromancy
{
/// Pure residual conversion after the ordinary result; no new corpse eligibility.
DLL_LINKAGE NecromancyResult reclaimResidualSkeletons(const NecromancyResult & ordinary,
	int conversionPercent, int32_t currentMana, int32_t manaLimit, const Policy * capturedPolicy = nullptr);
/// Recompute every output-derived field after choosing a complete batch.
DLL_LINKAGE void finalizeRaisedResult(NecromancyResult & result, int32_t currentMana, int32_t manaLimit,
	const Policy * capturedPolicy = nullptr);
DLL_LINKAGE void blockRaisedResult(NecromancyResult & result);
}
