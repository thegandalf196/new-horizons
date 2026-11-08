/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativePhysicalProtection.h"
#include "IDamageCalculatorScript.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusFilter.h"
#include "../bonuses/BonusList.h"
#include <map>

namespace nativeDamage
{
namespace
{
using CompiledFilter = std::pair<CSelector, std::string>;

const CompiledFilter & generalFilter(size_t subtype, std::optional<bool> shooting)
{
	// Immutable selectors/cache keys, not captured battle data. Compile exactly
	// the Lua API filters once; the bearer still answers every requested query.
	static const auto filters = []
	{
		std::array<CompiledFilter, 9> result;
		const std::array<std::string, 3> subtypes{"damageTypeAll", "damageTypeMelee", "damageTypeRanged"};
		for(size_t type = 0; type < subtypes.size(); ++type)
			for(size_t range = 0; range < 3; ++range)
			{
				BonusFilter filter;
				filter.type = "GENERAL_DAMAGE_REDUCTION";
				filter.subtype = subtypes[type];
				if(range != 0) filter.shooting = range == 2;
				result[type * 3 + range] = filter.compile();
			}
		return result;
	}();
	return filters[subtype * 3 + (shooting ? (*shooting ? 2 : 1) : 0)];
}

const CompiledFilter & physicalBasisFilter(bool shooting)
{
	static const auto filters = []
	{
		std::array<CompiledFilter, 2> result;
		for(size_t range = 0; range < result.size(); ++range)
		{
			BonusFilter filter;
			filter.type = "PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS";
			filter.shooting = range != 0;
			result[range] = filter.compile();
		}
		return result;
	}();
	return filters[shooting ? 1 : 0];
}

TConstBonusListPtr query(const battle::Unit & unit, const CompiledFilter & filter)
{
	return unit.getBonuses(filter.first, filter.second);
}

std::vector<double> sourceValues(const BonusList & bonuses, bool excludeSpellEffects)
{
	// Lua groups on getSourceID() strings, not VariantIdentifier numeric order.
	// Appending in source-list order preserves totalValue's native accumulation.
	std::map<std::pair<BonusSource, std::string>, BonusList> groups;
	for(const auto & bonus : bonuses)
		if(!excludeSpellEffects || bonus->source != BonusSource::SPELL_EFFECT)
			groups[{bonus->source, bonus->sid.toString()}].push_back(bonus);
	std::vector<double> result;
	result.reserve(groups.size());
	for(const auto & [key, group] : groups)
		result.push_back(group.totalValue());
	return result;
}

double defendRemaining(const DamageAttackInfo & info)
{
	if(!info.physicalDamage || info.shooting)
		return 1.0;
	return 1.0 - std::clamp(static_cast<double>(info.defensiveStanceDamageReductionIgnorePercent), 0.0, 100.0) / 100;
}
}

bool hasPhysicalDamageReductionStage(const DamageAttackInfo & info)
{
	return info.physicalDamage && info.physicalDamageReductionCapPercent >= 0;
}

PhysicalProtectionFacts capturePhysicalProtection(const DamageAttackInfo & info,
	bool hasGeneralReduction, bool hasPhysicalBasis)
{
	PhysicalProtectionFacts facts;
	if(hasPhysicalDamageReductionStage(info))
	{
		if(hasGeneralReduction)
		{
			facts.allDamageSourceValues = sourceValues(*query(*info.defender, generalFilter(0, info.shooting)), true);
			facts.subtypeSourceValues = sourceValues(*query(*info.defender,
				generalFilter(info.shooting ? 2 : 1, info.shooting)), false);
		}
		if(hasPhysicalBasis)
			facts.physicalBasisSourceValues = sourceValues(*query(*info.defender, physicalBasisFilter(info.shooting)), false);
		return facts;
	}
	if(hasGeneralReduction)
	{
		// Historical all/subtype filters deliberately have no combat-range filter.
		const auto all = query(*info.defender, generalFilter(0, std::nullopt));
		BonusList ordinary;
		for(const auto & bonus : *all)
			if(bonus->source != BonusSource::SPELL_EFFECT && (info.physicalDamage
				|| (bonus->stacking != "new-horizons:ordinary-creature-specialty:core:sorsha:shieldAndSword"
					&& bonus->stacking != "new-horizons:ordinary-creature-specialty:core:marius:demonicSolidarity")))
				ordinary.push_back(bonus);
		facts.legacyGeneralReduction = ordinary.totalValue();
		facts.magicShieldReduction = query(*info.defender, generalFilter(info.shooting ? 2 : 1, std::nullopt))->totalValue();
	}
	return facts;
}

double physicalDamageReductionFactor(const DamageAttackInfo & info, const PhysicalProtectionFacts & facts)
{
	double remaining = 1.0;
	const auto reduce = [&](double value, double scale)
	{
		const double fraction = std::clamp(value, 0.0, scale) / scale;
		remaining = remaining * (1 - fraction);
	};
	for(const auto value : facts.allDamageSourceValues) reduce(value, 100);
	for(const auto value : facts.subtypeSourceValues) reduce(value, 100);
	for(const auto value : facts.physicalBasisSourceValues) reduce(value, 10000);
	reduce(info.stoneboundPhysicalReductionBasisPoints, 10000);
	if(info.shooting) reduce(info.heavenlyGaleDamageReductionBasisPoints, 10000);
	reduce(info.newHorizonsArmorerReductionPercent, 100);
	if(info.intrinsicPhysicalDamageReductionPercent > 0) reduce(info.intrinsicPhysicalDamageReductionPercent, 100);
	reduce(info.runicEnduranceReductionPercent, 100);
	reduce(info.ironHideReductionPercent, 100);
	reduce(info.formationFightingReductionPercent, 100);
	reduce(info.paviseDamageReductionPercent, 100);
	const double defend = defendRemaining(info);
	reduce(info.battlecraftDefendReductionPercent * defend, 100);
	reduce(info.bulwarkDamageReductionBasisPoints * defend, 10000);
	if(!info.heroOrderDamageReductionPercents.empty())
		for(const auto value : info.heroOrderDamageReductionPercents) reduce(value, 100);
	else reduce(info.heroOrderDamageReductionPercent, 100);
	reduce(info.arcaneBulwarkReductionPercent, 100);
	const double cap = std::clamp(static_cast<double>(info.physicalDamageReductionCapPercent), 0.0, 100.0);
	const double capped = std::max(remaining, 1 - cap / 100);
	const double ignored = std::clamp(static_cast<double>(info.physicalDamageReductionIgnorePercent), 0.0, 100.0);
	return 1 - (1 - capped) * (1 - ignored / 100);
}

std::array<double, 5> legacyPhysicalProtectionFactors(const DamageAttackInfo & info, const PhysicalProtectionFacts & facts)
{
	if(hasPhysicalDamageReductionStage(info))
		return {};
	const double defend = defendRemaining(info);
	return {
		-(facts.legacyGeneralReduction + info.heroOrderDamageReductionPercent) / 100
			- (info.bulwarkDamageReductionBasisPoints * defend / 10000),
		-static_cast<double>(info.newHorizonsArmorerReductionPercent) / 100,
		-static_cast<double>(info.paviseDamageReductionPercent) / 100,
		-static_cast<double>(info.battlecraftDefendReductionPercent) * defend / 100,
		-facts.magicShieldReduction / 100
	};
}

double applyLegacyPhysicalProtection(const DamageAttackInfo & info, double damageFactor,
	bool usesStage, bool penetratesLegacy, double legacyMultiplier)
{
	if(info.physicalDamage && !usesStage)
		for(const double value : {info.intrinsicPhysicalDamageReductionPercent,
			info.runicEnduranceReductionPercent, info.ironHideReductionPercent})
			if(value > 0)
			{
				const double reduction = std::clamp(value, 0.0, 100.0) / 100;
				if(penetratesLegacy)
				{
					const double ignored = std::clamp(static_cast<double>(info.legacyPhysicalDamageReductionIgnorePercent), 0.0, 100.0) / 100;
					legacyMultiplier = legacyMultiplier * (1 - reduction * (1 - ignored));
				}
				else
				{
					const double ignored = std::clamp(static_cast<double>(info.physicalDamageReductionIgnorePercent), 0.0, 100.0) / 100;
					damageFactor = damageFactor * (1 - reduction * (1 - ignored));
				}
			}
	if(penetratesLegacy)
	{
		const double ignored = std::clamp(static_cast<double>(info.coordinatedPhysicalProtectionIgnorePercent), 0.0, 100.0) / 100;
		damageFactor = damageFactor * (1 - (1 - legacyMultiplier) * (1 - ignored));
	}
	return damageFactor;
}
}
