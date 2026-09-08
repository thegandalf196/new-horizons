/*
 * NewHorizonsMagic.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../json/JsonNode.h"
#include "../constants/EntityIdentifiers.h"
#include "NewHorizonsDirectDamage.h"

namespace newHorizonsMagic
{
constexpr int RULESET_VERSION = 1;
constexpr int DIRECT_DAMAGE_RULESET_VERSION = 2;
/// Empty snapshots retain legacy rules. Validation resolves canonical content
/// identity and requires non-NH common coverage. Present NH common rows require
/// v2; absent newly installed NH content never invalidates an older roster.
DLL_LINKAGE void validateRules(const JsonNode & rules);
/// Read only the supplied saved roster using the spell's canonical scoped key.
/// Absent snapshots/rows/formulas return null; no installed definition fallback.
DLL_LINKAGE std::optional<DirectDamageFormula> spellDirectDamage(const JsonNode & rules, const std::string & scopedIdentity);
/// Call only after checking explicit event overrides (including zero) and legacy
/// nonzero caster overrides. This accessor does not choose override precedence.
DLL_LINKAGE std::optional<int64_t> directDamageValue(const JsonNode & rules, const std::string & scopedIdentity, int32_t effectPower, int32_t divisor);
DLL_LINKAGE std::vector<SpellSchool> activeSchools(const JsonNode & rules);
DLL_LINKAGE std::vector<SpellSchool> spellSchools(const JsonNode & rules, SpellID spell);
DLL_LINKAGE int spellLevel(const JsonNode & rules, SpellID spell);
DLL_LINKAGE int spellCost(const JsonNode & rules, SpellID spell, int mastery);
DLL_LINKAGE int factionSpellWeight(const JsonNode & rules, FactionID faction, SpellID spell);
DLL_LINKAGE SecondarySkill replacementSkill(const JsonNode & rules, SecondarySkill skill);
DLL_LINKAGE bool skillAllowed(const JsonNode & rules, SecondarySkill skill, const std::set<SecondarySkill> & mapAllowed);
}
