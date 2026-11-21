/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <optional>

class JsonNode;
class CHero;
class CGHeroInstance;
struct Bonus;

namespace newHorizonsHeroes
{
struct SpecialtyPresentation;

/// A specialty value that applies at hero level 1 and grows by a fixed amount per
/// further level, up to a hard maximum.
struct DLL_LINKAGE LevelScaledValue
{
	int base = 0;
	int perLevel = 0;
	int maximum = 0;
	int at(int level) const;
};

/// Castle adventure-map specialties revised on 2026-11-20 (captured under
/// revisedSpecialties). Each row is independent; absence keeps the historical
/// specialty of that hero.
struct DLL_LINKAGE CastleAdventureSpecialties
{
	/// Sylvia: percent of her full daily Movement added on the first day of each week.
	std::optional<LevelScaledValue> forcedMarchMovementPercent;
	int forcedMarchMoralePenalty = 0;
};

DLL_LINKAGE void validateCastleAdventureSpecialties(const JsonNode & revisedRow);
DLL_LINKAGE std::optional<CastleAdventureSpecialties> castleAdventureSpecialties(const JsonNode & development);

/// Extra Movement percent Sylvia receives on the day `turn` days after today.
DLL_LINKAGE int forcedMarchMovementPercent(const CGHeroInstance & hero, int turn);
/// The Morale penalty to grant when Sylvia's weekly Forced March starts, or nothing
/// when it does not apply or one is already pending (penalties never stack).
DLL_LINKAGE std::optional<Bonus> forcedMarchMoralePenalty(const CGHeroInstance & hero);
DLL_LINKAGE bool isForcedMarchMoralePenalty(const Bonus & bonus);

/// True when this hero's captured Castle adventure specialty replaces every prototype
/// specialty bonus and skill-specialty marker of that hero.
DLL_LINKAGE bool retiresHistoricalCastleSpecialty(const CGHeroInstance & hero);
DLL_LINKAGE bool resolveCastleAdventurePresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
