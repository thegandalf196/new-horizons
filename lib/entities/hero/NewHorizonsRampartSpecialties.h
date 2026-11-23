/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "NewHorizonsCastleAdventureSpecialties.h"
#include "../creature/NewHorizonsCreatureCategoryRules.h"

#include <optional>

class JsonNode;
class CHero;
class CGHeroInstance;
class CGTownInstance;
class ResourceSet;
struct Bonus;

namespace newHorizonsHeroes
{
struct SpecialtyPresentation;

/// Kyrre: the highest guard rank she may pass beside, by hero level.
struct DLL_LINKAGE ElusiveRules
{
	int eliteLevel = 0;
	int championLevel = 0;
	bool operator==(const ElusiveRules &) const = default;
};

/// Uland: Regeneration after his Cure, in basis points of the stack's full Health per round.
struct DLL_LINKAGE LivingRemedyRules
{
	int rounds = 0;
	LevelScaledValue healingBasisPoints;
	bool operator==(const LivingRemedyRules &) const = default;
};

/// Gem: her first spell each combat is free up to spell level base + floor(hero level / step).
struct DLL_LINKAGE SorceressOfEnrothRules
{
	int baseSpellLevel = 0;
	int heroLevelsPerSpellLevel = 0;
	bool operator==(const SorceressOfEnrothRules &) const = default;
};

/// Melodia: Luck when the enemy army is stronger, + 1 per `levelsPerLuck` hero levels.
struct DLL_LINKAGE AgainstTheOddsRules
{
	int luck = 0;
	int levelsPerLuck = 0;
	int maximumLuck = 0;
	bool operator==(const AgainstTheOddsRules &) const = default;
};

/// The 2026-11-20 Rampart specialty table, captured under `revisedSpecialties`
/// (format 1166). An absent row keeps that hero's historical specialty, so a
/// world captured before these rows existed is unchanged. Scaled values apply at
/// hero level 1 and grow by a fixed amount per further level, up to a maximum.
struct DLL_LINKAGE RampartSpecialtyRules
{
	std::optional<LevelScaledValue> useTheGroundPermille; // Mephala: physical damage reduction
	std::optional<ElusiveRules> elusive; // Kyrre
	std::optional<LevelScaledValue> patronessPermille; // Jenova: share of the town's daily income
	std::optional<LevelScaledValue> giantSlayerPermillePerStep; // Coronius: damage per rank step
	std::optional<LivingRemedyRules> livingRemedy; // Uland
	std::optional<SorceressOfEnrothRules> sorceressOfEnroth; // Gem
	std::optional<LevelScaledValue> brittlePermille; // Alagar: extra physical damage taken
	std::optional<AgainstTheOddsRules> againstTheOdds; // Melodia
	std::optional<LevelScaledValue> perfectMountPercent; // Aeris: Pegasus magical damage reduction
	std::optional<LevelScaledValue> treantmootPercent; // Ryland: Dendroid physical damage reduction
	std::optional<LevelScaledValue> forestAmbushShotPercent; // Ivor: shot-from-hiding damage
	bool operator==(const RampartSpecialtyRules &) const = default;
};

/// Validates the Rampart children of an already field-checked `revisedSpecialties` row.
DLL_LINKAGE void validateRampartSpecialtyRows(const JsonNode & row);
DLL_LINKAGE RampartSpecialtyRules rampartSpecialtyRules(const JsonNode & development);

/// Current level values; zero or empty unless the hero is that specialist and its
/// world captured the row.
DLL_LINKAGE int useTheGroundPermille(const CGHeroInstance * hero);
/// Highest neutral guard rank Kyrre passes beside without a fight, if any.
DLL_LINKAGE std::optional<newHorizonsCreatures::CreatureCategory> elusiveGuardRank(const CGHeroInstance * hero);
DLL_LINKAGE int patronessPermille(const CGHeroInstance * hero);
/// Jenova's weekly collection on entering this town: the share of its daily income she
/// would receive now, or nothing when inactive, the town is not her player's or she
/// already collected there this week.
DLL_LINKAGE ResourceSet patronessCollection(const CGHeroInstance & hero, const CGTownInstance & town);
/// What her next entry into this town would collect: nothing while she is already
/// inside it (visiting or garrisoned), since only an arrival collects.
DLL_LINKAGE ResourceSet patronessOnNextEntry(const CGHeroInstance & hero, const CGTownInstance & town);
/// The marker recording that she collected at this town; it expires with the week.
DLL_LINKAGE Bonus patronessMarker(const CGHeroInstance & hero, const CGTownInstance & town);
DLL_LINKAGE int giantSlayerPermillePerStep(const CGHeroInstance * hero);
DLL_LINKAGE int livingRemedyBasisPoints(const CGHeroInstance * hero);
DLL_LINKAGE int livingRemedyRounds(const CGHeroInstance * hero);
/// Highest spell level Gem's first spell may have to be free (0 when inactive).
DLL_LINKAGE int sorceressFreeSpellLevel(const CGHeroInstance * hero);
DLL_LINKAGE int brittlePermille(const CGHeroInstance * hero);
DLL_LINKAGE int againstTheOddsLuck(const CGHeroInstance * hero);
DLL_LINKAGE std::optional<int> perfectMountPercent(const CGHeroInstance & hero);
DLL_LINKAGE std::optional<int> treantmootPercent(const CGHeroInstance & hero);
DLL_LINKAGE int forestAmbushShotPercent(const CGHeroInstance * hero);

/// Rank used by Elusive and Giant-Slayer: 0 Core, 1 Elite, 2 Champion. Creatures without a
/// captured rank (neutrals) rank by level: 1-3 Core, 4-6 Elite, 7 and above Champion.
/// -1 for creatures without a level (war machines).
DLL_LINKAGE int specialtyRank(const newHorizonsCreatures::CreatureCategoryRules & rules, CreatureID creature);

/// True when a captured Rampart row replaces this hero's historical specialty
/// (New and Replaced rows; the Changed and scaled rows keep their base specialty).
DLL_LINKAGE bool rampartSpecialtyRetiresHistorical(const CGHeroInstance & hero);
/// True when the hero's level-scaled passive creature bundle is a Rampart row.
DLL_LINKAGE bool rampartPassiveBundleScales(const CGHeroInstance & hero);
DLL_LINKAGE bool resolveRampartSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
