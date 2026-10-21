/*
 * NewHorizonsVengefulVines.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../battle/BattleHexArray.h"
#include "../battle/Destination.h"
#include "../constants/EntityIdentifiers.h"
#include "../json/JsonNode.h"
#include "effects/Effects.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace newHorizonsVengefulVines
{
inline constexpr std::string_view SPELL_KEY = "new-horizons:vengefulVines";

struct ProjectionDestination
{
	uint32_t unitId;
	BattleHex hex;
	bool operator==(const ProjectionDestination &) const = default;
};

struct ProjectionEffect
{
	std::string name;
	spells::effects::ForecastImplementation implementation;
	std::vector<ProjectionDestination> destinations;
	bool operator==(const ProjectionEffect &) const = default;
};

/// Owned ordered inputs to one completed forecast, never authoritative geometry.
/// Equality is sufficient only within the same frozen parent and cast context.
struct ProjectionReceipt
{
	int32_t concentrationBonusPercent = 0;
	int32_t effectLevel = 0;
	int32_t effectPower = 0;
	int64_t effectValue = 0;
	int32_t effectDuration = 0;
	int32_t spellPowerCoefficientBasisPoints = 0;
	int32_t castSpellPowerComponentBonusPercent = 0;
	std::vector<ProjectionEffect> effects;
	bool operator==(const ProjectionReceipt &) const = default;
};

/// Unknown or changed implementations fail closed, including after application.
DLL_LINKAGE bool supportsProjection(const spells::Mechanics & mechanics,
	const spells::effects::Effects::EffectsToApply & prepared);
DLL_LINKAGE std::optional<ProjectionReceipt> captureProjection(const spells::Mechanics & mechanics,
	const spells::effects::Effects::EffectsToApply & prepared, int32_t concentrationBonusPercent);

/// True only for the active Vengeful Vines entry in a saved New Horizons v3 roster.
DLL_LINKAGE bool enabled(const JsonNode & savedRules, SpellID spell);

/// Reads exactly three ordered, distinct, playable location destinations.
/// Each destination after the first must touch at least one earlier selection.
/// Unit targets, malformed order, and unavailable hexes return empty.
DLL_LINKAGE BattleHexArray footprint(const battle::Target & target);

/// Returns each connected three-hex selection once, in a deterministic valid click order.
/// Every result is suitable for constructing three ordered location destinations.
DLL_LINKAGE const std::vector<BattleHexArray> & connectedTriples();
}
