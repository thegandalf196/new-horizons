/*
 * NewHorizonsIndividualSpecialties.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <optional>

class JsonNode;
class CGHeroInstance;
class CHero;

namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
struct DLL_LINKAGE GraveReaperRules
{
	int percentagePointsPerLevel = 0;
	int maximumPercentagePoints = 0;
};

struct DLL_LINKAGE WarlocksFormulaRules
{
	int percentPerLevel = 0;
	int maximumPercent = 0;
};

/// Optional captured policy. Missing historical rules never use installed defaults.
struct DLL_LINKAGE IndividualSpecialtyRules
{
	GraveReaperRules graveReaper;
	WarlocksFormulaRules warlocksFormula;
};

DLL_LINKAGE std::optional<IndividualSpecialtyRules> individualSpecialtyRules(const JsonNode & development);
DLL_LINKAGE int cappedSpecialtyContribution(int level, int perLevel, int maximum);
DLL_LINKAGE int graveReaperPercentagePoints(const CGHeroInstance * hero);
DLL_LINKAGE int warlocksFormulaPercent(const CGHeroInstance * hero);
/// Preserve fractional percentage contributions: Basic +5% => 11050, not 11000.
DLL_LINKAGE bool resolveIndividualRiderPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
DLL_LINKAGE int strengthenedSpellcraftBasisPoints(int ordinaryEfficiencyPercent, int specialtyPercent);
}
