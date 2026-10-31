/*
 * SpellEffectPreview.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../Global.h"
#include <cstdint>
#include <optional>
#include <string>

class CSpell;
class CGHeroInstance;
class CBattleInfoCallback;

namespace newHorizonsMagic
{
/// Target-independent, read-only current-caster effect. No probabilities are rolled.
struct DLL_LINKAGE SpellEffectPreview
{
	struct Quantity
	{
		int64_t value = 0;
		int32_t scale = 1;
		std::string unitKey;
		auto operator<=>(const Quantity &) const = default;
	};
	std::string labelKey;
	std::string compactLabelKey;
	std::optional<Quantity> primary;
	std::optional<Quantity> secondary;
	std::string qualificationKey;
	std::string narrowLabelKey;
	auto operator<=>(const SpellEffectPreview &) const = default;
};

/// No battle construction, target enumeration, RNG, resource spending or mutation.
DLL_LINKAGE SpellEffectPreview spellEffectPreview(const CGHeroInstance & hero,
	const CSpell & spell, const CBattleInfoCallback * battle = nullptr,
	bool metamagicFollowup = false);
}
