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
class CBattleInfoCallback;
class BattleHex;
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
constexpr int SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION = 3;
constexpr int CURRENT_RULESET_VERSION = SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
constexpr int SPELL_POINTS_RULESET_VERSION = 1;
constexpr int MAGE_GUILD_GENERATION_RULESET_VERSION = 1;
constexpr int SPELL_POWER_COEFFICIENT_BASIS_POINTS = 10'000;
constexpr int SPELLCRAFT_ARCANE_FOCUS_BONUS_PERCENT = 20;
constexpr int SPELL_POINTS_INTELLIGENCE_MAXIMUM_PERCENT = 130;
constexpr int BLESS_BASE_DURATION = 2;
constexpr int BLESS_MAX_DURATION = 4;
constexpr int BLESS_SPELL_POWER_DURATION_DIVISOR = 80;
constexpr int SLOW_BASE_DURATION_ROUNDS = 2;
constexpr int CURSE_BASE_DURATION_ROUNDS = 3;
constexpr int SORROW_BASE_DURATION_ROUNDS = 3;
constexpr int DOOM_BASE_DURATION_ROUNDS = 3;
constexpr int DOOM_BASE_CRIPPLING_PERCENT = 35;
constexpr int DOOM_MAX_CRIPPLING_PERCENT = 60;
constexpr int DOOM_SPELL_POWER_TERM_NUMERATOR = 3;
constexpr int DOOM_SPELL_POWER_TERM_DIVISOR = 20;
constexpr int SORROW_SPELL_POWER_PER_MORALE = 70;
constexpr int SORROW_BASE_MORALE_PENALTY = 1;
constexpr int SORROW_MAX_MORALE_PENALTY = 3;
constexpr int VAMPIRISM_BASE_DURATION_ROUNDS = 3;
constexpr int VAMPIRISM_BASE_HEAL_BASIS_POINTS = 2'500;
constexpr int VAMPIRISM_MAX_BASE_HEAL_BASIS_POINTS = 5'000;
constexpr int VAMPIRISM_NIGHT_FEEDER_BONUS_BASIS_POINTS = 1'500;
constexpr int VAMPIRISM_MAX_HEAL_BASIS_POINTS = 6'500;
constexpr int VAMPIRISM_SPELL_POWER_BASIS_POINTS_PER_POINT = 15;
constexpr int REANIMATE_BASE_HEALING_HP = 220;
constexpr int REANIMATE_SPELL_POWER_HP_PER_POINT = 5;
constexpr int REANIMATOR_BONUS_PERCENT = 25;
constexpr int CHAIN_LIGHTNING_FIXED_TARGET_COUNT_V3 = 5;
constexpr int QUICKSAND_BASE_PATCH_COUNT_V3 = 2;
constexpr int QUICKSAND_MAX_PATCH_COUNT_V3 = 5;
constexpr int QUICKSAND_SPELL_POWER_PER_PATCH_V3 = 60;
constexpr int METAMAGIC_FORMULA_RESERVE_POINTS = 3;
constexpr int METAMAGIC_SPELL_BUFFER_POINTS = 6;
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
inline constexpr std::string_view METAMAGIC_SPELL_BUFFER = "new-horizons:metamagic.spellBuffer";
inline constexpr std::string_view METAMAGIC_SPELL_ECHO = "new-horizons:metamagic.spellEcho";
inline constexpr std::string_view METAMAGIC_GRAND = "new-horizons:metamagic.grandMetamagic";
inline constexpr std::string_view METAMAGIC_PERFECT_SEQUENCE = "new-horizons:metamagic.perfectSequence";
inline constexpr std::string_view HAVOC_STORMCALLER = "new-horizons:havocMagic.stormcaller";
inline constexpr std::string_view HAVOC_CONDUCTOR = "new-horizons:havocMagic.conductor";
inline constexpr std::string_view HAVOC_ANNIHILATOR = "new-horizons:havocMagic.annihilator";
inline constexpr std::string_view LIGHT_MAGIC_SKILL = "new-horizons:lightMagic";
inline constexpr std::string_view LIGHT_BENEDICTION = "new-horizons:lightMagic.benediction";
inline constexpr std::string_view SPELLCRAFT_SKILL = "new-horizons:spellcraft";
inline constexpr std::string_view SPELLCRAFT_ARCANE_FOCUS = "new-horizons:spellcraft.arcaneFocus";
inline constexpr std::string_view SPELLCRAFT_EMPOWER_SPELL = "new-horizons:spellcraft.empowerSpell";
constexpr int SPELLCRAFT_EMPOWER_MANA_THRESHOLD = 12;
constexpr int SPELLCRAFT_EMPOWER_BONUS_PERCENT = 25;
inline constexpr std::string_view NATURE_POISON_SPELL = "new-horizons:poison";
inline constexpr std::string_view NATURE_REGENERATION_SPELL = "new-horizons:regeneration";
inline constexpr std::string_view SHADOW_LIFE_DRAIN_SPELL = "new-horizons:lifeDrain";
inline constexpr std::string_view SHADOW_GIFT_SPELL = "new-horizons:shadowGift";
inline constexpr std::string_view SHADOW_VAMPIRISM_SPELL = "new-horizons:vampirism";
inline constexpr std::string_view SHADOW_VAMPIRISM_STATUS = "core:vampirism";
inline constexpr std::string_view SHADOW_REANIMATE_SPELL = "new-horizons:reanimate";
inline constexpr std::string_view SHADOW_SOUL_REAPER_SPELL = "new-horizons:soulReaper";
inline constexpr std::string_view SHADOW_DOOM_SPELL = "new-horizons:doom";
inline constexpr std::string_view SHADOW_REANIMATOR_PERK = "new-horizons:shadowMagic.reanimator";
inline constexpr std::string_view SHADOW_MAGIC_SKILL = "new-horizons:shadowMagic";
inline constexpr std::string_view SHADOW_DARK_GIFT_PERK = "new-horizons:shadowMagic.darkGift";
inline constexpr std::string_view SHADOW_NIGHT_FEEDER_PERK = "new-horizons:shadowMagic.nightFeeder";
inline constexpr std::string_view NATURE_MAGIC_SKILL = "new-horizons:natureMagic";
inline constexpr std::string_view NATURE_HERBALIST = "new-horizons:natureMagic.herbalist";
inline constexpr std::string_view STORM_OF_DAGGERS_SPELL = "new-horizons:stormOfDaggers";
constexpr int REGENERATION_MARK_SCALE = 1'000'000;
constexpr int REGENERATION_BASE_RATE_MILLIONTHS = 250'000;
constexpr int REGENERATION_MAX_RATE_MILLIONTHS = 500'000;
constexpr int REGENERATION_HERBALIST_BONUS_MILLIONTHS = 100'000;
constexpr int STORM_OF_DAGGERS_MAX_TARGETS = 5;
constexpr int STORM_OF_DAGGERS_EXTRA_TARGET_DAMAGE_PERCENT = 15;

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
/// True only for saved v3 New Horizons battles, where Berserk targets one
/// enemy creature stack. Older profiles retain core LOCATION/area targeting.
DLL_LINKAGE bool berserkUsesSingleCreatureTarget(const JsonNode & rules);
/// True only for saved v3 battles, where Dispel uses New Horizons' friend-or-foe
/// single-stack effect instead of core targeting and Expert obstacle removal.
DLL_LINKAGE bool dispelUsesNewHorizonsRules(const JsonNode & rules);
/// True only when the saved-v3 roster contains canonical Level-1 Shadow Curse
/// at its canonical mastery costs. Earlier snapshots keep their recorded spell.
DLL_LINKAGE bool curseRulesEnabled(const JsonNode & rules, SpellID spell);
/// True only when the saved-v3 roster contains canonical Level-1 Shadow Sorrow
/// at its fixed four-Mana cost. Earlier snapshots keep their recorded spell.
DLL_LINKAGE bool sorrowRulesEnabled(const JsonNode & rules, SpellID spell);
/// True only when the saved-v3 roster contains canonical Shadow Gift.
/// Legacy snapshots never acquire newly installed Shadow Gift content.
DLL_LINKAGE bool shadowGiftEnabled(const JsonNode & rules, SpellID spell);
/// Canonical saved-v3 Curse / Sorrow durations, including Malediction when
/// selected. Legacy, missing, or non-canonical saved spell rows return nullopt.
DLL_LINKAGE std::optional<int> curseDurationRounds(const JsonNode & rules, const CGHeroInstance * hero,
	SpellID spell);
DLL_LINKAGE std::optional<int> sorrowDurationRounds(const JsonNode & rules, const CGHeroInstance * hero,
	SpellID spell);
/// Saved-v3 canonical Vampirism identity gate. Older profiles cannot acquire
/// this new spell from installed content alone.
DLL_LINKAGE bool vampirismEnabled(const JsonNode & rules, SpellID spell);
/// Returns Vampirism lifesteal in basis points, including saved School ×
/// Spellcraft, cast-specific Warcasting/Empower, and Night Feeder after the
/// ordinary 50% cap. Returns null for legacy or non-canonical spell rows.
DLL_LINKAGE std::optional<int> vampirismHealBasisPoints(const JsonNode & rules,
	const CGHeroInstance * hero, SpellID spell, int32_t rawSpellPower,
	int warcastingBonusPercent = 0, int empowerSpellBonusPercent = 0,
	int additionalSpellPowerComponentPercent = 0);
/// Saved-v3 canonical Re-animate identity gate. Older snapshots never acquire
/// the newly registered Shadow spell from installed content alone.
DLL_LINKAGE bool reanimateEnabled(const JsonNode & rules, SpellID spell);
/// True only when the hero has selected Reanimator at its registered rank.
DLL_LINKAGE bool hasReanimatorPerk(const CGHeroInstance * hero);
/// Integer Re-animate HP pool, including Reanimator's 25% bonus after
/// satisfying surviving-unit wounds. School × Spellcraft is kept exact until
/// the base pool's final HP floor; legacy/non-canonical rows return nullopt.
DLL_LINKAGE std::optional<int64_t> reanimateHealingPool(const JsonNode & rules,
	const CGHeroInstance * hero, SpellID spell, int32_t rawSpellPower, int64_t survivorWounds,
	int additionalSpellPowerComponentPercent = 0);
/// Saved-v3 canonical Soul Reaper identity gate. Older snapshots never acquire
/// the newly registered Shadow spell from installed content alone.
DLL_LINKAGE bool soulReaperEnabled(const JsonNode & rules, SpellID spell);
/// Saved-v3 canonical Doom identity and cost gate. Earlier snapshots cannot
/// acquire the newly registered Shadow spell from installed content alone.
DLL_LINKAGE bool doomRulesEnabled(const JsonNode & rules, SpellID spell);
/// Doom's capped integer crippling magnitude. Only the Spell Power-derived
/// term receives saved School × Spellcraft scaling; invalid/legacy spell rows
/// return nullopt.
DLL_LINKAGE std::optional<int> doomCripplingPenaltyPercent(const JsonNode & rules,
	const CGHeroInstance * hero, SpellID spell, int32_t rawSpellPower,
	int additionalSpellPowerComponentPercent = 0);
/// Soul Reaper's 40% missing-effective-HP component. Current HP includes any
/// temporary hit points; missing HP is clamped to zero when current exceeds max.
DLL_LINKAGE std::optional<int64_t> soulReaperMissingHealthDamage(const JsonNode & rules,
	SpellID spell, int64_t effectiveMaximumHP, int64_t currentHP);
/// Increase a positive post-mitigation hit to lethal damage when the target
/// would otherwise remain at or below 10% effective maximum HP. The caller
/// supplies current HP including temporary hit points. This is ordinary
/// damage and does not mark the casualties as unusable remains.
DLL_LINKAGE int64_t soulReaperDamageAfterExecution(int64_t effectiveMaximumHP,
	int64_t currentHP, int64_t postMitigationDamage);
/// Applies the saved v3 fixed-five Chain Lightning target count while keeping
/// the configured, mastery-dependent value for legacy/v1/v2 battles.
DLL_LINKAGE int chainLightningTargetCount(const JsonNode & rules, SpellID spell, int configuredTargetCount);
/// True when this v3 saved battle uses the New Horizons single-target Expert
/// range for one of the 23 core spells whose vanilla Expert data is Mass.
/// Legacy/v1/v2 snapshots and every other spell retain vanilla static spell data.
DLL_LINKAGE bool expertRangeIsSingleTarget(const JsonNode & rules, SpellID spell);
DLL_LINKAGE bool mageGuildGenerationActive(const JsonNode & rules);
DLL_LINKAGE int mageGuildSpellsAtLevel(const JsonNode & rules, int level);
DLL_LINKAGE std::vector<SpellSchool> preferredSchools(const JsonNode & rules, FactionID faction);
/// Explicit multiplicative physical-reduction model; -1 preserves historical calculations.
DLL_LINKAGE int physicalDamageReductionCapPercent(const JsonNode & rules);
/// Master Chain Lightning retains 75% of the previous hop at level zero and
/// gains one percentage point per hero level, capped at 90%.  The helper keeps
/// the displayed value aligned with the authoritative Lua effect.
DLL_LINKAGE int masterChainLightningRetentionPercent(int heroLevel);
/// Applies Bless's ordinary v3 duration floor/cap to an already-scaled
/// Spell-Power term. Duration bonuses and perks are added by the caller after
/// this ordinary cap.
DLL_LINKAGE int blessDurationFromPowerTerm(int64_t spellPowerTerm);
DLL_LINKAGE bool hasBenedictionPerk(const CGHeroInstance * hero);
/// Dark Gift reduces Shadow Gift's actual HP sacrifice, never its selected damage tier.
DLL_LINKAGE bool hasDarkGiftPerk(const CGHeroInstance * hero);
/// Returns a hero-contextual spell description for presentation surfaces.
/// Legacy saves and all other spells retain the ordinary static description.
DLL_LINKAGE std::string spellDescriptionForHero(const CGHeroInstance * hero,
	const spells::Spell * spell, int schoolLevel);
/// Read only the supplied saved roster using the spell's canonical scoped key.
/// Absent snapshots/rows/formulas return null; no installed definition fallback.
DLL_LINKAGE std::optional<DirectDamageFormula> spellDirectDamage(const JsonNode & rules, const std::string & scopedIdentity);
/// Call only after checking explicit event overrides (including zero) and legacy
/// nonzero caster overrides. This accessor does not choose override precedence.
DLL_LINKAGE std::optional<int64_t> directDamageValue(const JsonNode & rules, const std::string & scopedIdentity,
	int32_t effectPower, int32_t divisor, int coefficientPercent = 100);
/// Saved v3 school-rank Spell Power coefficient, or 100% for v1/v2 snapshots.
/// Rank indexes are none=0, Basic=1, Advanced=2, Expert=3.
DLL_LINKAGE int schoolRankPowerCoefficientPercent(const JsonNode & rules, int schoolRank);
/// Optional saved-v3 Spellcraft efficiency by the actual registered Skill's
/// rank. Omission in an older v3 snapshot, or use with v1/v2 rules, means 100%.
DLL_LINKAGE int spellcraftEfficiencyPercent(const JsonNode & rules, int spellcraftRank);
/// School-only percentage for the highest-ranked school on an ordinary spell
/// in the saved roster. Multi-school spells use one highest rank; adventure
/// spells, creature abilities, excluded spells and legacy snapshots retain 100%.
DLL_LINKAGE int spellPowerCoefficientPercent(const JsonNode & rules, const CGHeroInstance * hero, SpellID spell);
/// Exact School × Spellcraft coefficient in basis points for the Spell-Power-
/// derived term: 10000 is 100%. No rounding is done while composing factors.
/// Spellcraft is read from the hero's registered new-horizons:spellcraft Skill.
/// The final optional percentage (0..100) applies after both saved factors and
/// affects only the Spell-Power-derived term (for example, Arcane Focus).
DLL_LINKAGE int spellPowerCoefficientBasisPoints(const JsonNode & rules, const CGHeroInstance * hero,
	SpellID spell, int additionalSpellPowerComponentPercent = 0);
/// Saved-v3 Sorrow's positive Morale penalty magnitude, or nullopt for legacy,
/// missing, or non-canonical saved spell rows. The School × Spellcraft factors,
/// Warcasting, and Empower scale raw Hero Spell Power before the final /70
/// floor. Sorrow deliberately ignores the legacy primary-growth divisor.
DLL_LINKAGE std::optional<int> sorrowMoralePenalty(const JsonNode & rules, const CGHeroInstance * hero,
	SpellID spell, int32_t rawSpellPower, int warcastingBonusPercent = 0,
	int empowerSpellBonusPercent = 0, int additionalSpellPowerComponentPercent = 0);
/// Saved-v3 Quicksand count, or nullopt for any other spell/profile. School,
/// Spellcraft, Warcasting, and Empower scale only the Spell-Power term.
DLL_LINKAGE std::optional<int> quicksandPatchCount(const JsonNode & rules, const CGHeroInstance * hero,
	SpellID spell, int32_t spellPower, int32_t spellPowerDivisor = 1,
	int warcastingBonusPercent = 0, int empowerSpellBonusPercent = 0,
	int additionalSpellPowerComponentPercent = 0);
/// True only when the saved v3 spell row opts into player-selected Quicksand
/// placement. Markerless v3 snapshots retain their random legacy placement.
DLL_LINKAGE bool quicksandSelectedPlacementEnabled(const JsonNode & rules, SpellID spell);
/// Checks whether an available battlefield hex is empty, accessible ground
/// suitable for an authoritative Quicksand patch placement.
DLL_LINKAGE bool quicksandPlacementHexIsLegal(const CBattleInfoCallback & battle, const BattleHex & hex);
/// Empower Spell's additive multiplier for this saved ordinary hero cast. The
/// threshold uses only listed cost × the explicit variant × Wisdom, before
/// battle creature auras, Metamagic reductions, or Overcharge are applied.
DLL_LINKAGE int empowerSpellBonusPercent(const JsonNode & rules, const CGHeroInstance * hero, SpellID spell,
	int listedCostMultiplier = 1);
/// Regeneration's saved-rate snapshot. School rank and Warcasting affect only
/// the Spell Power term; Herbalist adds ten percentage points before the cap.
DLL_LINKAGE int32_t regenerationRateMillionths(int32_t spellPower, int schoolRankCoefficientPercent,
	bool herbalist, int warcastingBonusPercent = 0);
/// Basis-point counterpart preserving fractional School × Spellcraft products
/// until the Regeneration rate's final integer floor.
DLL_LINKAGE int32_t regenerationRateMillionthsBasisPoints(int32_t spellPower, int coefficientBasisPoints,
	bool herbalist, int warcastingBonusPercent = 0, int empowerSpellBonusPercent = 0);
/// Resolve fixed-point Regeneration marks into healable surviving creature wounds.
DLL_LINKAGE int64_t regenerationHealAmount(int64_t pendingMicroHealth, int64_t survivingWounds);
/// New Horizons' detailed Sorcery rules make the existing Magic Arrow an
/// adjustable spell.  The optional overcharge is deliberately enabled only
/// for a saved roster which classifies the canonical core spell as Sorcery;
/// legacy worlds therefore retain the original fixed-cost/fixed-effect cast.
DLL_LINKAGE bool magicArrowOverchargeEnabled(const JsonNode & rules, SpellID spell);
/// True only when the saved New Horizons row explicitly opts Cure into its
/// single-target, selected-physical-affliction behavior. Missing settings keep
/// older snapshots on the original Cure mechanics.
DLL_LINKAGE bool cureEnabled(const JsonNode & rules, SpellID spell);
/// True only for the saved v3 New Horizons hero Poison row. It applies the
/// shared physical-affliction state; the core creature ability is unchanged.
DLL_LINKAGE bool physicalPoisonEnabled(const JsonNode & rules, SpellID spell);
/// Poison's fixed 20 base plus half of the saved-rank-scaled Spell Power term.
/// Integer damage truncates fractional health down, matching the combat damage pipeline.
DLL_LINKAGE int64_t poisonBaseDamage(int32_t spellPower, int schoolRankCoefficientPercent);
/// Basis-point counterpart preserving fractional School × Spellcraft products
/// until Poison's final integer damage floor.
DLL_LINKAGE int64_t poisonBaseDamageBasisPoints(int32_t spellPower, int coefficientBasisPoints,
	int empowerSpellBonusPercent = 0);
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
	int32_t spellPower, int32_t divisor, int overcharge, MagicArrowOverchargeModifiers modifiers = {},
	int coefficientPercent = 100, int empowerSpellBonusPercent = 0);
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
/// School, Spellcraft, and cast-specific Arcane Focus scale only the Spell
/// Power term through the supplied coefficient; fixed minimum and cap remain.
/// The caller must only use this while a saved NH magic roster is active.
DLL_LINKAGE int landMineHexCount(int32_t spellPower,
	int coefficientBasisPoints = SPELL_POWER_COEFFICIENT_BASIS_POINTS);
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
