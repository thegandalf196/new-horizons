/*
 * NewHorizonsSchoolSpellcraft.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "NewHorizonsIndividualSpecialties.h"
#include "../../constants/EntityIdentifiers.h"
#include <optional>

class JsonNode;
class CGHeroInstance;
class CHero;
namespace spells { class IBattleCast; }

namespace newHorizonsHeroes
{
enum class SchoolSpellcraftKind { OVERCASTER, DARK_SAVANT, PRIMAL_FORMULA, MIRE_HEXER };
enum class SpellcraftComponent { NUMERICAL, DAMAGE, DIRECT_DAMAGE };
enum class SpellcraftSpecialtyScope { NONE, NUMERICAL, DAMAGE, DIRECT_DAMAGE };

struct DLL_LINKAGE SpellcraftSpecialtyFactors
{
	int schoolPercent = 100;
	int ordinaryEfficiencyPercent = 100;
	int enhancedEfficiencyBasisPoints = 10000;
	SpellcraftSpecialtyScope scope = SpellcraftSpecialtyScope::NONE;
	bool operator==(const SpellcraftSpecialtyFactors &) const = default;
};

/// Transient original-event context, copied with BattleCast, never a saved status.
/// An explicitly empty context prevents later derivatives recapturing a new rider.
struct DLL_LINKAGE SpellcraftCastContext
{
	bool originalHero = false;
	std::optional<SpellcraftSpecialtyFactors> factors;
	bool operator==(const SpellcraftCastContext &) const = default;
};

DLL_LINKAGE void validateSchoolSpellcraftSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE std::optional<WarlocksFormulaRules> schoolSpellcraftRules(
	const JsonNode & development, SchoolSpellcraftKind kind);
DLL_LINKAGE SpellcraftCastContext captureSpellcraftContext(const JsonNode & magic,
	const CGHeroInstance * hero, SpellID spell, bool originalHero);
DLL_LINKAGE SpellcraftCastContext resolveSpellcraftContext(const spells::IBattleCast * event);
DLL_LINKAGE int composeSpellcraftCoefficient(const SpellcraftSpecialtyFactors & captured,
	SpellcraftComponent component, int additionalSpellPowerComponentPercent);
DLL_LINKAGE bool resolveSchoolSpellcraftPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
