/*
 * HeroCommandCoefficient.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../bonuses/Bonus.h"
#include "../constants/EntityIdentifiers.h"

class CGHeroInstance;
class JsonNode;

namespace heroCommands
{
/// Actual immutable hero policy/perk state plus explicit rating query source.
/// No CGHeroInstance clone, inferred ownership, mutable callback or field cache.
/// The caller retains the real metadata/source interval through evaluation.
class DLL_LINKAGE CoefficientInputs
{
	const CGHeroInstance & hero;
	TConstBonusListPtr projectedPrimary;
	CoefficientInputs(const CGHeroInstance & hero, TConstBonusListPtr projectedPrimary);
public:
	static CoefficientInputs native(const CGHeroInstance & hero);
	static CoefficientInputs projected(const CGHeroInstance & policy, TConstBonusListPtr primaryBonuses);
	const CGHeroInstance & policy() const { return hero; }
	int primary(PrimarySkill skill) const;
};

DLL_LINKAGE int attributeEfficiencyPercent(const CGHeroInstance & hero, PrimarySkill attribute,
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent, bool includeCommanderPerks);
DLL_LINKAGE int coefficient(const JsonNode & effect, const CoefficientInputs & inputs,
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent, bool includeCommanderPerks = true);
}
