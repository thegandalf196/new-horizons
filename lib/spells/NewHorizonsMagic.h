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

#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "../json/JsonNode.h"
#include "../constants/EntityIdentifiers.h"
#include "NewHorizonsDirectDamage.h"

class CGHeroInstance;
class ResourceSet;

namespace battle
{
class Unit;
}

namespace spells
{
class Spell;
}

namespace newHorizonsMagic
{
constexpr int RULESET_VERSION = 1;
constexpr int DIRECT_DAMAGE_RULESET_VERSION = 2;
constexpr int SPELL_POINTS_RULESET_VERSION = 1;
constexpr int SPELL_POINTS_INTELLIGENCE_MAXIMUM_PERCENT = 130;
constexpr int DIRECT_DAMAGE_POWER_DIVISOR = 10;
constexpr int COUNTERSPELL_LISTED_COST = 11;
inline constexpr std::string_view METAMAGIC_SKILL = "new-horizons:metamagic";
inline constexpr std::string_view METAMAGIC_SPELL_SEQUENCING = "new-horizons:metamagic.spellSequencing";
inline constexpr std::string_view METAMAGIC_ARCANE_ECONOMY = "new-horizons:metamagic.arcaneEconomy";
inline constexpr std::string_view METAMAGIC_FOCUSED_PAIRING = "new-horizons:metamagic.focusedPairing";
inline constexpr std::string_view METAMAGIC_COUNTERSEQUENCE = "new-horizons:metamagic.countersequence";
inline constexpr std::string_view METAMAGIC_ECHOED_DURATION = "new-horizons:metamagic.echoedDuration";
inline constexpr std::string_view METAMAGIC_SPLIT_FOCUS = "new-horizons:metamagic.splitFocus";
inline constexpr std::string_view METAMAGIC_FORMULA_RESERVE = "new-horizons:metamagic.formulaReserve";
inline constexpr std::string_view METAMAGIC_SPELL_ECHO = "new-horizons:metamagic.spellEcho";
inline constexpr std::string_view METAMAGIC_GRAND = "new-horizons:metamagic.grandMetamagic";
inline constexpr std::string_view METAMAGIC_PERFECT_SEQUENCE = "new-horizons:metamagic.perfectSequence";
inline constexpr std::string_view HAVOC_STORMCALLER = "new-horizons:havocMagic.stormcaller";
inline constexpr std::string_view HAVOC_CONDUCTOR = "new-horizons:havocMagic.conductor";
inline constexpr std::string_view HAVOC_ANNIHILATOR = "new-horizons:havocMagic.annihilator";

struct DLL_LINKAGE AdventureSpellState
{
	bool castToday = false;

	template <typename Handler> void serialize(Handler & h)
	{
		h & castToday;
	}
};

constexpr uint32_t INVALID_METAMAGIC_TARGET = std::numeric_limits<uint32_t>::max();
/// Canonical New Horizons Land Mine thresholds.  The spell uses the caster's
/// saved Spell Power (before applying the direct-damage divisor) to determine
/// how many distinct battlefield hexes the action must contain.
constexpr int LAND_MINE_THREE_HEX_POWER = 100;
constexpr int LAND_MINE_FOUR_HEX_POWER = 200;
/// Empty snapshots retain legacy rules. Validation resolves canonical content
/// identity and requires non-NH common coverage. Present NH common rows require
/// v2; absent newly installed NH content never invalidates an older roster.
DLL_LINKAGE void validateRules(const JsonNode & rules);
/// True only when the saved magic-rules snapshot opts into the Normal/Buffer
/// Spell Point model. Installed configuration never activates it for a legacy save.
DLL_LINKAGE bool spellPointRulesActive(const JsonNode & rules);
/// Saved Intelligence capacity multiplier for the opted-in Spell Point rules.
DLL_LINKAGE int32_t spellPointsIntelligenceMaximumPercent(const JsonNode & rules);
/// True when the supplied saved battle snapshot uses New Horizons magic.
/// This is intentionally state-backed; installed content alone must not alter
/// legacy saves.
DLL_LINKAGE bool rulesActive(const JsonNode & rules);
/// Master Chain Lightning retains 75% of the previous hop at level zero and
/// gains one percentage point per hero level, capped at 90%.  The helper keeps
/// the displayed value aligned with the authoritative Lua effect.
DLL_LINKAGE int masterChainLightningRetentionPercent(int heroLevel);
/// Returns a hero-contextual spell description for presentation surfaces.
/// Legacy saves and all other spells retain the ordinary static description.
DLL_LINKAGE std::string spellDescriptionForHero(const CGHeroInstance * hero,
	const spells::Spell * spell, int schoolLevel);
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
/// True only when the saved New Horizons row explicitly opts Cure into its
/// single-target, selected-physical-affliction behavior. Missing settings keep
/// older snapshots on the original Cure mechanics.
DLL_LINKAGE bool cureEnabled(const JsonNode & rules, SpellID spell);
/// Saved Cure source identities whose complete SPELL_EFFECT source groups are
/// currently present on this unit. Results are sorted by SpellID for stable UI
/// and AI enumeration; legacy/unspecified Cure profiles return no candidates.
DLL_LINKAGE std::vector<SpellID> cureAfflictions(const JsonNode & rules, const battle::Unit * unit);
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
/// Returns the six canonical Magic School Skills captured by the saved rules.
/// Legacy worlds have no New Horizons school-skill catalogue.
DLL_LINKAGE std::vector<SecondarySkill> schoolSkills(const JsonNode & rules);
/// Required canonical school rank for an ordinary spell: none for levels 1-2,
/// Basic/Advanced/Expert for levels 3/4/5. Adventure and legacy spells have no
/// New Horizons school-proficiency requirement.
DLL_LINKAGE int requiredSchoolRank(const JsonNode & rules, SpellID spell);
/// Canonical school Skill identities associated with this saved spell entry.
/// Empty for legacy, excluded, or neutral Adventure spells.
DLL_LINKAGE std::vector<SecondarySkill> spellSchoolSkills(const JsonNode & rules, SpellID spell);
/// True when the hero may learn or cast this spell under the saved six-school
/// rules. A multi-school spell is accessible through any one qualifying school.
/// This deliberately does not decide whether the hero owns a spell source.
DLL_LINKAGE bool hasSchoolProficiency(const CGHeroInstance * hero, SpellID spell);
DLL_LINKAGE std::vector<SpellSchool> spellSchools(const JsonNode & rules, SpellID spell);
DLL_LINKAGE int spellLevel(const JsonNode & rules, SpellID spell);
DLL_LINKAGE int spellCost(const JsonNode & rules, SpellID spell, int mastery);
/// Applies canonical Wisdom to an ordinary spell's listed cost. Multipliers
/// (for example a Mass variant) are applied before the percentage discount.
/// Optional paid additions such as Overcharge are deliberately not passed here.
DLL_LINKAGE int wisdomAdjustedCost(int listedCost, int listedCostMultiplier, int wisdomRank);
/// Returns the canonical Wisdom rank only for a saved New Horizons ruleset.
/// Legacy Wisdom never acquires New Horizons cost semantics.
DLL_LINKAGE int wisdomRank(const CGHeroInstance * hero);
/// True when the saved New Horizons roster treats the spell as one of the
/// neutral, adventure-map spells. These entries deliberately have no school
/// membership and are not subject to school mastery or Wisdom discounts.
DLL_LINKAGE bool isAdventureSpell(const JsonNode & rules, SpellID spell);
/// Return the canonical fixed cost for a saved neutral adventure spell.
/// Calling this for a non-adventure spell is an invalid rules query.
DLL_LINKAGE int adventureSpellCost(const JsonNode & rules, SpellID spell);
/// True when the saved snapshot contains the New Horizons Adventure Magic
/// roster. Empty/legacy snapshots retain the original adventure-spell rules.
DLL_LINKAGE bool adventureSpellRulesActive(const JsonNode & rules);
/// Resolve the single canonical Adventure Spell assigned to this Guild tier
/// in the saved roster. Legacy snapshots and invalid/unavailable tiers return
/// SpellID::NONE; this never falls back to installed configuration.
DLL_LINKAGE SpellID adventureSpellForGuildLevel(const JsonNode & rules, int guildLevel);
/// Return the validated canonical Guild tier for a saved Adventure Spell.
/// Legacy, unavailable, or malformed entries have no tier.
DLL_LINKAGE std::optional<int> adventureSpellGuildLevel(const JsonNode & rules, SpellID spell);
/// Return the town unlock price captured in the saved rules. Missing or
/// malformed unlockCost data is rejected; it is never interpreted as free.
DLL_LINKAGE ResourceSet adventureSpellUnlockCost(const JsonNode & rules, SpellID spell);
/// True only for the canonical core Land Mine identity.  Spell indices remain
/// stable in the saved protocol, but the identity check keeps this helper
/// independent of installed mod ordering.
DLL_LINKAGE bool isLandMine(SpellID spell);
/// True only for the canonical core Fire Wall identity.  The saved roster
/// still decides whether the New Horizons behavior is active; this helper is
/// intentionally limited to stable spell identity checks.
DLL_LINKAGE bool isFireWall(SpellID spell);
/// Number of player-selected empty hexes required by canonical NH Land Mine.
/// The caller must only use this while a saved NH magic roster is active.
DLL_LINKAGE int landMineHexCount(int32_t spellPower);
/// True only for the saved New Horizons Counterspell identity.  The identity
/// check is deliberately content-based so installed spell indices remain
/// irrelevant to saved battles.
DLL_LINKAGE bool isCounterspell(const spells::Spell * spell);
/// Counterspell's ward cost, using the enemy spell's listed/base cost rather
/// than any battlefield discount. Countermage or Countersequence changes 2x
/// to ceil(1.75x).
DLL_LINKAGE int counterspellCost(int listedCost, bool countermage, bool countersequence = false);
/// Saved-rules-backed Metamagic identity and perk helpers.  These are kept in
/// the shared magic layer so server, client previews, and BattleAI use exactly
/// the same rank/perk gates.
DLL_LINKAGE int metamagicRank(const CGHeroInstance * hero);
DLL_LINKAGE bool hasMetamagicPerk(const CGHeroInstance * hero, std::string_view perkId);
/// Stormcaller enhances only the Spell Power-derived part of Lightning Bolt,
/// Chain Lightning, and Master Chain Lightning. The saved-rules/perk check
/// keeps legacy Solmyr and legacy spell damage unchanged.
DLL_LINKAGE bool hasStormcallerPerk(const CGHeroInstance * hero, const spells::Spell * spell);
/// Annihilator makes Disintegrate ignore 20% of the target's magical damage
/// reduction.  The saved rules/perk check keeps legacy worlds unchanged.
DLL_LINKAGE bool hasAnnihilatorPerk(const CGHeroInstance * hero, const spells::Spell * spell);
DLL_LINKAGE int factionSpellWeight(const JsonNode & rules, FactionID faction, SpellID spell);
DLL_LINKAGE SecondarySkill replacementSkill(const JsonNode & rules, SecondarySkill skill);
DLL_LINKAGE bool skillAllowed(const JsonNode & rules, SecondarySkill skill, const std::set<SecondarySkill> & mapAllowed);
}
