/*
 * ActivatedOrdinarySpecialties.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../lib/json/JsonNode.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

/// One extended ordinary creature specialty whose coefficients 0.93.1 ships.
struct ActivatedOrdinarySpecialty
{
	const char * hero;
	const char * upgraded;
	const char * base;
	const char * title;
	std::map<std::string, int> coefficients;
};

inline const std::vector<ActivatedOrdinarySpecialty> & activatedOrdinarySpecialties()
{
	static const std::vector<ActivatedOrdinarySpecialty> rows{
		{"core:tyris", "core:champion", "core:cavalier", "Lance Formation",
			{{"lanceFormationDefensePerHex", 1}, {"lanceFormationMaximumDefense", 5}}},
		{"core:ingham", "core:zealot", "core:monk", "Consecrated Target", {{"consecratedTargetDefenseIgnorePercent", 25}}},
		{"core:rashka", "core:efreetSultan", "core:efreet", "Cinder Reprisal", {{"cinderReprisalDamagePercent", 20}}},
		{"core:marius", "core:hornedDemon", "core:demon", "Demonic Solidarity", {{"demonicSolidarityReductionPercent", 20}}},
		{"core:calh", "core:magog", "core:gog", "Scorching Impact", {{"scorchingImpactDamageReductionPercent", 20}}},
		{"core:nymus", "core:pitLord", "core:pitFiend", "Cruel Overseer", {{"cruelOverseerDefenseReduction", 3}}},
		{"core:moandor", "core:powerLich", "core:lich", "Grave Dust", {{"graveDustHealingReductionBasisPoints", 5000}}},
		{"core:synca", "core:scorpicore", "core:manticore", "Toxic Sting", {{"toxicStingChancePercent", 20}}},
		{"core:shiva", "core:thunderbird", "core:roc", "Storm Wings",
			{{"stormWingsDefenseIgnorePercent", 25}, {"stormWingsMinimumFlightDistance", 4}}},
		{"core:drakon", "core:gnollMarauder", "core:gnoll", "Gnoll Harriers", {{"gnollHarriersDamagePercent", 20}}},
		{"core:ignissa", "core:energyElemental", "core:fireElemental", "Emberbrand", {{"emberbrandDamagePercent", 20}}},
		{"core:monere", "new-horizons:wispUpgrade", "new-horizons:wisp", "Prismatic Vanguard",
			{{"prismaticVanguardPhysicalDamagePercent", 20}}},
	};
	return rows;
}

/// Extended rows whose accepted faction tables change them; inactive until that faction's beta.
inline const std::vector<std::pair<const char *, const char *>> & deferredOrdinarySpecialties()
{
	static const std::vector<std::pair<const char *, const char *>> rows{
		{"core:lorelei", "featheredFeintPhysicalReductionIgnorePercent"},
		{"core:alkin", "ironHideReductionPercent"},
	};
	return rows;
}

/// A capture made before 0.93.1 (formats 1164 and 1165) carries none of these coefficients.
inline void omitActivatedOrdinaryCoefficients(JsonNode & heroRules)
{
	auto & policy = heroRules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	for(const auto & row : activatedOrdinarySpecialties())
		for(const auto & [key, value] : row.coefficients)
			policy.Struct().erase(key);
}
