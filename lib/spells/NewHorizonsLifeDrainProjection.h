/*
 * NewHorizonsLifeDrainProjection.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../battle/BattleHex.h"
#include "effects/Effects.h"
#include <optional>
#include <string>
#include <string_view>
namespace spells { class Mechanics; }
namespace newHorizonsLifeDrain
{
inline constexpr std::string_view SPELL_KEY = "new-horizons:lifeDrain";
/// Same frozen parent/caster only. The secondary identity is intentionally absent:
/// AI must ALSO prove its completed native updates were semantic no-ops.
struct ProjectionReceipt
{
	uint32_t primaryTarget;
	BattleHex primaryPosition;
	std::string effectName;
	int32_t concentrationBonusPercent;
	int32_t effectLevel;
	int32_t effectPower;
	int64_t effectValue;
	int32_t effectDuration;
	int32_t spellPowerCoefficientBasisPoints;
	int32_t castSpellPowerComponentBonusPercent;
	bool operator==(const ProjectionReceipt &) const = default;
};
DLL_LINKAGE bool supportsProjection(const spells::Mechanics & mechanics,
	const spells::effects::Effects::EffectsToApply & prepared);
DLL_LINKAGE std::optional<ProjectionReceipt> captureProjection(const spells::Mechanics & mechanics,
	const spells::effects::Effects::EffectsToApply & prepared, const spells::Target & originalAim,
	int32_t concentrationBonusPercent);
}
