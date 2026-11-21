/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <optional>

class JsonNode;
class CHero;
class CGHeroInstance;

namespace newHorizonsHeroes
{
struct SpecialtyPresentation;

/// One level-scaled percentage: the base is the hero level 1 value and grows by
/// perLevel for each further level, capped: min(maximum, base + perLevel x (level - 1)).
/// Values are tenths of a percent so Quick Study's +2.5%/level stays exact.
struct DLL_LINKAGE LevelPercent
{
	int basePermille = 0;
	int permillePerLevel = 0;
	int maximumPermille = 0;
	int permilleAt(int level) const;
	int percentAt(int level) const { return permilleAt(level) / 10; }
	bool operator==(const LevelPercent &) const = default;
};

struct DLL_LINKAGE FrostMantleRules
{
	LevelPercent returned;
	int rounds = 0;
	int initiativePenaltyPercent = 0;
	int chillRounds = 0;
	bool operator==(const FrostMantleRules &) const = default;
};

struct DLL_LINKAGE QuickStudyRules
{
	LevelPercent spellPower;
	int secondUseLevel = 0;
	bool operator==(const QuickStudyRules &) const = default;
};

struct DLL_LINKAGE PariahRules
{
	int morale = 0;
	LevelPercent shadowPowerBonus; // extra Spell Power term percentage
	bool operator==(const PariahRules &) const = default;
};

/// The combat half of the 2026-11-20 Castle specialty table, captured under
/// `revisedSpecialties` (format 1166). Each absent row keeps that hero's
/// historical specialty, so 1164 and 1165 worlds are unchanged.
struct DLL_LINKAGE CastleSpecialtyRules
{
	std::optional<LevelPercent> volleyCommand; // Orrin: volley shot damage
	std::optional<LevelPercent> aerialSupremacy; // Edric: aerial counter damage bonus
	std::optional<LevelPercent> benedictionOfArms; // Adela: prayer potency bonus
	std::optional<PariahRules> pariah; // Cuthbert
	std::optional<FrostMantleRules> frostMantle; // Adelaide
	std::optional<QuickStudyRules> quickStudy; // Sanya
	std::optional<LevelPercent> sermon; // Loynis: prayer potency bonus
	bool operator==(const CastleSpecialtyRules &) const = default;
};

/// Validates the Castle children of an already field-checked `revisedSpecialties` row.
DLL_LINKAGE void validateCastleSpecialtyRows(const JsonNode & row);
DLL_LINKAGE CastleSpecialtyRules castleSpecialtyRules(const JsonNode & development);

/// Per-hero captured rules; each is empty unless the hero is that specialist and
/// its world captured the row.
DLL_LINKAGE const std::optional<LevelPercent> & volleyCommandRule(const CGHeroInstance * hero);
DLL_LINKAGE const std::optional<LevelPercent> & revisedAerialSupremacyRule(const CGHeroInstance * hero);
DLL_LINKAGE const std::optional<LevelPercent> & benedictionOfArmsRule(const CGHeroInstance * hero);
DLL_LINKAGE const std::optional<PariahRules> & pariahRule(const CGHeroInstance * hero);
DLL_LINKAGE const std::optional<FrostMantleRules> & frostMantleRule(const CGHeroInstance * hero);
DLL_LINKAGE const std::optional<QuickStudyRules> & quickStudySpecialtyRule(const CGHeroInstance * hero);
DLL_LINKAGE const std::optional<LevelPercent> & sermonRule(const CGHeroInstance * hero);

/// Current level values; zero when the specialty is inactive.
DLL_LINKAGE int volleyCommandDamagePercent(const CGHeroInstance * hero);
DLL_LINKAGE int aerialCounterDamagePercent(const CGHeroInstance * hero);
/// Prayer potency bonus of Adela's Benediction of Arms or Loynis's Sermon grants.
DLL_LINKAGE int specialtyPrayerPotencyPercent(const CGHeroInstance * hero);
/// Potency Faith after a specialty potency bonus: potency x (100 + bonus) / 100.
DLL_LINKAGE int boostedPotencyFaith(int potencyFaith, int bonusPercent);
DLL_LINKAGE int pariahShadowPowerPercent(const CGHeroInstance * hero);
DLL_LINKAGE int frostMantleReturnedPercent(const CGHeroInstance * hero);
DLL_LINKAGE int quickStudySpellPowerPermille(const CGHeroInstance * hero);
DLL_LINKAGE int quickStudyUses(const CGHeroInstance * hero);

/// Pariah's permanent army Morale, replacing any earlier copy; no-op for other heroes.
DLL_LINKAGE void installCastleSpecialtyBonuses(CGHeroInstance & hero);
/// True when a captured Castle row replaces this hero's historical specialty bonus.
DLL_LINKAGE bool castleSpecialtyRetiresHistorical(const CGHeroInstance & hero);
DLL_LINKAGE bool castleSpecialtyRetiresHistorical(const CHero & hero, const JsonNode & development);
DLL_LINKAGE bool resolveCastleSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
