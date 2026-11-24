/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "NewHorizonsCastleAdventureSpecialties.h"
#include "../../constants/EntityIdentifiers.h"
#include "../../GameConstants.h"

#include <array>
#include <optional>

class JsonNode;
class CHero;
class CGHeroInstance;
class ResourceSet;
class CCreatureSet;

namespace newHorizonsHeroes
{
struct SpecialtyPresentation;

/// Neela: each hit adds `hitPermille` damage reduction until the stack's next activation,
/// up to a total that grows with her level.
struct DLL_LINKAGE HardenedRules
{
	int hitPermille = 0;
	LevelScaledValue totalPermille;
	bool operator==(const HardenedRules &) const = default;
};

/// Rissa: 1 Mercury per max(minimum, goldPerMercury - perLevel x level) Gold of destroyed enemies.
struct DLL_LINKAGE SpoilsOfAlchemyRules
{
	int goldPerMercury = 0;
	int goldPerMercuryPerLevel = 0;
	int minimumGoldPerMercury = 0;
	int maximumMercury = 0;
	bool operator==(const SpoilsOfAlchemyRules &) const = default;
};

/// Astral: each cast makes that spell `percentPerCast` stronger, at most `capPercentPerLevel` x level.
struct DLL_LINKAGE PracticeMakesPerfectRules
{
	int percentPerCast = 0;
	int capPercentPerLevel = 0;
	bool operator==(const PracticeMakesPerfectRules &) const = default;
};

/// Aine: daily Gold = Mana x min(maximum, base + perLevel x level) / 1000.
struct DLL_LINKAGE WellspringOfWealthRules
{
	int goldPerManaPermille = 0;
	int goldPerManaPermillePerLevel = 0;
	int maximumGoldPerManaPermille = 0;
	bool operator==(const WellspringOfWealthRules &) const = default;
};

/// Solmyr: Lightning Bolt and Chain Lightning Spell Power damage bonus; Chain Lightning learned at a level.
struct DLL_LINKAGE StormLordRules
{
	LevelScaledValue damagePercent;
	int chainLightningLevel = 0;
	bool operator==(const StormLordRules &) const = default;
};

/// Halon's Spellweaver: Double Cast uses added per combat, raised from a hero level.
/// RETIRED (2026-11-25): no hero receives it in a new capture; kept so a capture that holds
/// the row still loads and plays. See isRetiredRevisedSpecialtyRow and the wiki Graveyard page.
struct DLL_LINKAGE SpellweaverRules
{
	int additionalUses = 0;
	int increasedAdditionalUses = 0;
	int increasedFromLevel = 0;
	bool operator==(const SpellweaverRules &) const = default;
};

/// Theodorus: Mana regained at the start of every combat round after the first, in permille of
/// his maximum Mana (rounded down, at least `minimumMana`, never above the maximum).
struct DLL_LINKAGE InexhaustibleKnowledgeRules
{
	LevelScaledValue manaPermille;
	int minimumMana = 0;
	bool operator==(const InexhaustibleKnowledgeRules &) const = default;
};

/// Thane: raw Spell Power and Knowledge added on each gained level.
struct DLL_LINKAGE SageOfTheAgesRules
{
	int spellPowerPerLevel = 0;
	int knowledgePerLevel = 0;
	bool operator==(const SageOfTheAgesRules &) const = default;
};

/// The 2026-11-20 Academy specialty table, captured under `revisedSpecialties`
/// (format 1166). An absent row keeps that hero's historical specialty, so a world
/// captured before these rows existed (0.94.0-beta.1 and earlier) is unchanged.
struct DLL_LINKAGE AcademySpecialtyRules
{
	std::optional<HardenedRules> hardened; // Neela
	std::optional<SpoilsOfAlchemyRules> spoilsOfAlchemy; // Rissa
	std::optional<PracticeMakesPerfectRules> practiceMakesPerfect; // Astral
	std::optional<WellspringOfWealthRules> wellspringOfWealth; // Aine
	std::optional<StormLordRules> stormLord; // Solmyr
	std::optional<LevelScaledValue> serpentsEmbraceBoundPercent; // Fafner: Naga damage to targets it binds
	std::optional<LevelScaledValue> goldStandardDiscountPercent; // Josephine: Iron-to-Gold Golem upgrade
	std::optional<InexhaustibleKnowledgeRules> inexhaustibleKnowledge; // Theodorus
	std::optional<SpellweaverRules> spellweaver; // Halon; retired, never shipped in new captures
	bool halonNullifyingShot = false; // Theodorus's Nullifying Shot belongs to Halon
	std::optional<LevelScaledValue> focusMagicPermille; // Cyra: extra Focus Magic ranged damage
	std::optional<SageOfTheAgesRules> sageOfTheAges; // Thane
	bool operator==(const AcademySpecialtyRules &) const = default;
};

/// Validates the Academy children of an already field-checked `revisedSpecialties` row.
DLL_LINKAGE void validateAcademySpecialtyRows(const JsonNode & row);
DLL_LINKAGE AcademySpecialtyRules academySpecialtyRules(const JsonNode & development);

/// Current level values; zero or empty unless the hero is that specialist and its
/// world captured the row.
DLL_LINKAGE int hardenedHitPermille(const CGHeroInstance * hero);
DLL_LINKAGE int hardenedTotalPermille(const CGHeroInstance * hero);
/// Gold of destroyed enemy recruitment value per Mercury (0 when inactive).
DLL_LINKAGE int spoilsGoldPerMercury(const CGHeroInstance * hero);
/// Mercury Rissa gains for destroying enemy creatures worth `destroyedGold` Gold.
DLL_LINKAGE int spoilsOfAlchemyMercury(const CGHeroInstance * hero, int64_t destroyedGold);
/// Recruitment Gold of an army's creatures, war machines excluded.
DLL_LINKAGE int64_t spoilsRecruitmentGold(const CCreatureSet & army);
/// Astral: the bonus cap per spell at his current level, in percent (0 when inactive).
DLL_LINKAGE int practiceCapPercent(const CGHeroInstance * hero);
DLL_LINKAGE int practicePercentPerCast(const CGHeroInstance * hero);
/// Astral's current practice bonus for one spell, in percent (0 when inactive or unpracticed).
DLL_LINKAGE int practicePercent(const CGHeroInstance * hero, SpellID spell);
/// The bonus his next cast of `spell` reaches, or nothing when it would not change.
DLL_LINKAGE std::optional<int> practiceAfterCast(const CGHeroInstance * hero, SpellID spell);
/// Replaces the hero's saved practice marker for `spell` (an ordinary permanent hero bonus,
/// so no state is added to the save format). Applied by SetNewHorizonsSpellPractice.
DLL_LINKAGE void setSpellPractice(CGHeroInstance & hero, SpellID spell, int percent);
/// Aine's Gold per Mana in permille at her level (0 when inactive).
DLL_LINKAGE int wellspringGoldPerManaPermille(const CGHeroInstance * hero);
/// Gold Aine gains at the start of the day, from her current Mana.
DLL_LINKAGE int wellspringOfWealthGold(const CGHeroInstance & hero);
DLL_LINKAGE int stormLordDamagePercent(const CGHeroInstance * hero);
/// The level at which Solmyr learns Chain Lightning (0 when inactive).
DLL_LINKAGE int stormLordChainLightningLevel(const CGHeroInstance * hero);
DLL_LINKAGE bool isStormLordSpell(SpellID spell);
DLL_LINKAGE int serpentsEmbraceBoundPercent(const CGHeroInstance * hero);
DLL_LINKAGE int goldStandardDiscountPercent(const CGHeroInstance * hero);
/// Mana Theodorus regains at the start of a later round (0 when inactive or already full).
DLL_LINKAGE int inexhaustibleKnowledgeMana(const CGHeroInstance & hero);
/// The hero whose Magi carry Nullifying Shot in this hero's world: Halon once the swap is
/// captured, Theodorus otherwise.
DLL_LINKAGE const char * nullifyingShotHero(const CGHeroInstance * hero);
/// Halon's retired Spellweaver uses, when a capture holds that row and he does not hold
/// Nullifying Shot; nothing otherwise (the Double Cast policy's flat +1 then applies).
DLL_LINKAGE std::optional<int> spellweaverAdditionalUses(const CGHeroInstance * hero);
/// True when Halon holds Nullifying Shot, which replaces his flat extra Double Cast use.
DLL_LINKAGE bool halonHoldsNullifyingShot(const CGHeroInstance * hero);
/// Cyra's extra Focus Magic ranged damage in permille, or nothing when the row is not captured.
DLL_LINKAGE std::optional<int> focusMagicSpecialtyPermille(const CGHeroInstance * hero);
/// Thane's raw primary gains per gained level, or nothing when the row is not captured.
DLL_LINKAGE std::optional<std::array<int, GameConstants::PRIMARY_SKILLS>> sageOfTheAgesGrowth(const CGHeroInstance & hero);

/// Solmyr learns Chain Lightning once he reaches its level; no-op otherwise.
DLL_LINKAGE void learnStormLordSpells(CGHeroInstance & hero);
/// True when Storm Lord makes Chain Lightning an ordinary spell for this hero again.
DLL_LINKAGE bool stormLordAllowsSpell(const CGHeroInstance & hero, SpellID spell);

/// True when a captured Academy row replaces this hero's historical specialty
/// (New and Replaced rows; the Changed rows keep their base specialty).
DLL_LINKAGE bool academySpecialtyRetiresHistorical(const CGHeroInstance & hero);
DLL_LINKAGE bool resolveAcademySpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
