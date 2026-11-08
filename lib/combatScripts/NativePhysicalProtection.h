/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once

#include <array>
#include <vector>

struct DamageAttackInfo;

namespace nativeDamage
{
/// Owned observations from the canonical physicalProtection.lua bonus queries.
/// Source groups retain numeric source / lexical source-ID order, and their
/// values have already passed through the native BonusList value-type rules.
struct PhysicalProtectionFacts
{
	std::vector<double> allDamageSourceValues;
	std::vector<double> subtypeSourceValues;
	std::vector<double> physicalBasisSourceValues;
	double legacyGeneralReduction = 0;
	double magicShieldReduction = 0;
};

PhysicalProtectionFacts capturePhysicalProtection(const DamageAttackInfo & info,
	bool hasGeneralReduction, bool hasPhysicalBasis);

// Pure folds: these read only payload scalars and owned facts, never unit pointers.
bool hasPhysicalDamageReductionStage(const DamageAttackInfo & info);
double physicalDamageReductionFactor(const DamageAttackInfo & info, const PhysicalProtectionFacts & facts);
std::array<double, 5> legacyPhysicalProtectionFactors(const DamageAttackInfo & info, const PhysicalProtectionFacts & facts);
double applyLegacyPhysicalProtection(const DamageAttackInfo & info, double damageFactor,
	bool usesStage, bool penetratesLegacy, double legacyMultiplier);
}
