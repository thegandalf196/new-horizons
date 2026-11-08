/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "MechanicsAuthoring.h"
namespace nativeRules
{
using LegacyPassiveProducer = std::function<std::vector<std::shared_ptr<Bonus>>()>;
/// Called once from the existing rank loader after its source identity is known.
/// Authored and legacy descriptors are mutually exclusive, not concatenated.
DLL_LINKAGE std::vector<std::shared_ptr<Bonus>> rankEffectsOrLegacy(const JsonNode & capturedRank,
	SecondarySkill source, const LegacyPassiveProducer & legacy);
/// Cold original-army attachment adapter; caller supplies its existing native stack identity.
DLL_LINKAGE std::vector<std::shared_ptr<Bonus>> familyEffectsOrLegacy(const JsonNode & capturedRow,
	HeroTypeID source, std::string stacking, const FamilyResolver & resolveFamily,
	const LegacyPassiveProducer & legacy);
/// Existing Timed native effect owner selects this fixed slot after resolving the spell.
/// It retains eligibility, cleanup, specialty scaling, caster stamps and packet ordering.
DLL_LINKAGE std::optional<PreparedMechanics> prepareTimedSource(const JsonNode & capturedRow,
	SpellID source, TimedSourceSlot slot);
}
