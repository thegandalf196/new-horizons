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

class CGHeroInstance;

namespace spells
{
class Spell;
}

namespace newHorizonsMagic
{
constexpr int RULESET_VERSION = 1;
constexpr int DIRECT_DAMAGE_RULESET_VERSION = 2;
constexpr int DIRECT_DAMAGE_POWER_DIVISOR = 10;
constexpr int COUNTERSPELL_LISTED_COST = 11;
/// Canonical New Horizons Land Mine thresholds.  The spell uses the caster's
/// saved Spell Power (before applying the direct-damage divisor) to determine
/// how many distinct battlefield hexes the action must contain.
constexpr int LAND_MINE_THREE_HEX_POWER = 100;
constexpr int LAND_MINE_FOUR_HEX_POWER = 200;
/// Empty snapshots retain legacy rules. Validation resolves canonical content
/// identity and requires non-NH common coverage. Present NH common rows require
/// v2; absent newly installed NH content never invalidates an older roster.
DLL_LINKAGE void validateRules(const JsonNode & rules);
/// True when the supplied saved battle snapshot uses New Horizons magic.
/// This is intentionally state-backed; installed content alone must not alter
/// legacy saves.
DLL_LINKAGE bool rulesActive(const JsonNode & rules);
/// Read only the supplied saved roster using the spell's canonical scoped key.
/// Absent snapshots/rows/formulas return null; no installed definition fallback.
DLL_LINKAGE std::optional<DirectDamageFormula> spellDirectDamage(const JsonNode & rules, const std::string & scopedIdentity);
/// Call only after checking explicit event overrides (including zero) and legacy
/// nonzero caster overrides. This accessor does not choose override precedence.
DLL_LINKAGE std::optional<int64_t> directDamageValue(const JsonNode & rules, const std::string & scopedIdentity, int32_t effectPower, int32_t divisor);
/// New Horizons' detailed Sorcery rules make the existing Magic Arrow an
/// adjustable spell.  The optional overcharge is deliberately enabled only
/// for a saved roster which classifies the canonical core spell as Sorcery;
/// legacy worlds therefore retain the original fixed-cost/fixed-effect cast.
DLL_LINKAGE bool magicArrowOverchargeEnabled(const JsonNode & rules, SpellID spell);
struct DLL_LINKAGE MagicArrowOverchargeModifiers
{
	int maximumBonus = 0;
	/// Percentage in tenths: 175 means 17.5% per selected point.
	int damagePercentTenths = 150;

	bool operator==(const MagicArrowOverchargeModifiers &) const = default;
};
DLL_LINKAGE MagicArrowOverchargeModifiers magicArrowOverchargeModifiers(const CGHeroInstance * hero);
/// Returns a saved-perk duration adjustment for an ordinary hero cast.
/// Explicit BattleCast duration overrides are handled by the caller and must
/// not be modified by this helper.
DLL_LINKAGE int spellDurationBonus(const CGHeroInstance * hero, SpellID spell);
DLL_LINKAGE int magicArrowMaxOvercharge(const JsonNode & rules, SpellID spell, int32_t spellPower,
	MagicArrowOverchargeModifiers modifiers = {});
/// Returns the raw pre-resistance damage for a legal overcharge selection.
/// The divisor is the caster's New Horizons primary-rating coefficient scale.
DLL_LINKAGE std::optional<int64_t> magicArrowDamage(const JsonNode & rules, SpellID spell,
	int32_t spellPower, int32_t divisor, int overcharge, MagicArrowOverchargeModifiers modifiers = {});
DLL_LINKAGE std::vector<SpellSchool> activeSchools(const JsonNode & rules);
DLL_LINKAGE std::vector<SpellSchool> spellSchools(const JsonNode & rules, SpellID spell);
DLL_LINKAGE int spellLevel(const JsonNode & rules, SpellID spell);
DLL_LINKAGE int spellCost(const JsonNode & rules, SpellID spell, int mastery);
/// True only for the canonical core Land Mine identity.  Spell indices remain
/// stable in the saved protocol, but the identity check keeps this helper
/// independent of installed mod ordering.
DLL_LINKAGE bool isLandMine(SpellID spell);
/// Number of player-selected empty hexes required by canonical NH Land Mine.
/// The caller must only use this while a saved NH magic roster is active.
DLL_LINKAGE int landMineHexCount(int32_t spellPower);
/// True only for the saved New Horizons Counterspell identity.  The identity
/// check is deliberately content-based so installed spell indices remain
/// irrelevant to saved battles.
DLL_LINKAGE bool isCounterspell(const spells::Spell * spell);
/// Counterspell's ward cost, using the enemy spell's listed/base cost rather
/// than any battlefield discount. Countermage changes 2x to ceil(1.75x).
DLL_LINKAGE int counterspellCost(int listedCost, bool countermage);
DLL_LINKAGE int factionSpellWeight(const JsonNode & rules, FactionID faction, SpellID spell);
DLL_LINKAGE SecondarySkill replacementSkill(const JsonNode & rules, SecondarySkill skill);
DLL_LINKAGE bool skillAllowed(const JsonNode & rules, SecondarySkill skill, const std::set<SecondarySkill> & mapAllowed);
}
