/*
 * HeroClassLean.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/GameConstants.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/nativeRules/CompiledDevelopmentRules.h"

#include <array>
#include <cstdint>
#include <optional>

/// Might/Magic lean of a hero CLASS, shown under the hero window's class line.
/// It is a fixed property of the class's per-level growth: it never reads the
/// hero's current, base or equipped attributes, so level, artifacts and spells
/// cannot change it.
namespace heroClassLean
{
using Growth = std::array<int, GameConstants::PRIMARY_SKILLS>;

struct Lean
{
	int mightPercent = 50;
	int magicPercent = 50;
	bool operator==(const Lean &) const = default;
};

/// Might = Attack + Defense growth; Magic = Spell Power + Knowledge growth.
/// Might rounds half up and Magic is the remainder, so the two always sum to
/// 100. A zero total, or any negative part, shows an even 50/50.
inline Lean fromGrowth(const Growth & growth)
{
	const int64_t might = static_cast<int64_t>(growth[0]) + growth[1];
	const int64_t magic = static_cast<int64_t>(growth[2]) + growth[3];
	const int64_t total = might + magic;
	if(growth[0] < 0 || growth[1] < 0 || growth[2] < 0 || growth[3] < 0 || total <= 0)
		return {};
	const int mightPercent = static_cast<int>((might * 200 + total) / (total * 2));
	return {mightPercent, 100 - mightPercent};
}

/// The class growth this hero levels with: the saved New Horizons class
/// profile when primary growth is active, otherwise the class's original
/// low-level growth chances. Both are class data, never hero attributes.
inline std::optional<Growth> classGrowth(const CGHeroInstance & hero)
{
	if(const auto & profile = hero.compiledDevelopment()->profile())
		return profile->growth;
	const auto * heroClass = hero.getHeroClass();
	if(!heroClass || heroClass->primarySkillLowLevel.size() < GameConstants::PRIMARY_SKILLS)
		return std::nullopt;
	Growth growth{};
	for(size_t index = 0; index < growth.size(); ++index)
		growth[index] = heroClass->primarySkillLowLevel[index];
	return growth;
}

inline std::optional<Lean> forHero(const CGHeroInstance & hero)
{
	const auto growth = classGrowth(hero);
	if(!growth)
		return std::nullopt;
	return fromGrowth(*growth);
}
}
