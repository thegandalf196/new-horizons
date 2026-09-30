/*
 * NewHorizonsBlink.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../battle/BattleHex.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace battle
{
class Unit;
}

namespace spells
{
class Mechanics;
}

namespace newHorizonsBlink
{
inline constexpr const char * SPELL_ID = "new-horizons:blink";
inline constexpr const char * CHAOS_SKILL = "new-horizons:chaosMagic";
inline constexpr const char * BLINKMASTER_PERK = "new-horizons:chaosMagic.blinkmaster";

constexpr int BASE_RADIUS = 2;
constexpr int MAX_RADIUS = 4;
constexpr int SPELL_POWER_PER_RADIUS = 100;

/// The target's landing radius and the exact legal head-hex destinations for
/// one saved-v3 Blink cast. Destinations are unique and ascending by hex id.
struct DLL_LINKAGE Preview
{
	int radius = BASE_RADIUS;
	std::vector<BattleHex> legalDestinations;
	bool blinkmaster = false;
};

/// An exact discrete distribution over final Blink destinations. `weight / totalWeight`
/// is the probability of that destination after applying Blinkmaster, when present.
struct DLL_LINKAGE WeightedDestination
{
	BattleHex hex;
	uint32_t weight = 0;
	uint32_t totalWeight = 0;
};

/// Radius = min(4, 2 + floor(scaled Spell Power / 100)). The caller supplies
/// the saved School x Spellcraft coefficient and Warcasting percentage. Blink
/// deliberately does not apply a legacy effect-power divisor or Empower.
DLL_LINKAGE int radiusFor(int32_t rawSpellPower, int32_t coefficientBasisPoints,
	int32_t warcastingBonusPercent);

/// Returns nullopt when the cast is not saved-v3 Blink or `unit` is not a live,
/// valid creature target in this battle. Geometry checks only landing footprints;
/// they do not path through intermediate hexes.
DLL_LINKAGE std::optional<Preview> preview(const spells::Mechanics & mechanics, const battle::Unit * unit);

/// Resolves one Blinkmaster pair by greater distance from `origin`; equal
/// distances deterministically select the lower BattleHex id.
DLL_LINKAGE BattleHex fartherDestination(const BattleHex & origin, const BattleHex & first,
	const BattleHex & second);

/// Computes the final endpoint probability distribution. For ordinary Blink
/// this is uniform; Blinkmaster enumerates all ordered independent draws with
/// replacement so AI evaluation can score the exact outcome without RNG.
DLL_LINKAGE std::vector<WeightedDestination> outcomeDistribution(const Preview & preview,
	const BattleHex & origin);
}
