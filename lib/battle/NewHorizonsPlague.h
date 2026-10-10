/*
 * NewHorizonsPlague.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string_view>

#include "BattleSide.h"

class CBattleInfoCallback;
class JsonNode;
struct Bonus;

namespace battle
{
class Unit;
}

namespace newHorizonsPlague
{
constexpr std::string_view SPELL_ID = "new-horizons:plague";
inline constexpr auto SKILL = "new-horizons:shadowMagic";
inline constexpr auto PLAGUEBEARER = "new-horizons:shadowMagic.plaguebearer";
DLL_LINKAGE int32_t normalPropagationLimit(const JsonNode & rules);
DLL_LINKAGE void validateRuleSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE int32_t capturedPropagationLimit(const Bonus & marker);
DLL_LINKAGE bool containsExtendedPropagation(const JsonNode & node);
constexpr int SPELL_POWER_COEFFICIENT_BASIS_POINTS = 10'000;

/// True when this stack currently carries the dispellable magical Plague marker.
DLL_LINKAGE bool hasPlague(const battle::Unit * unit);

/// Returns the deterministic next adjacent, alive, uninfected stack ID. Creature
/// biology is intentionally irrelevant: Constructs, Elementals, Undead, and Demons
/// remain eligible if they are otherwise receptive to this magical affliction.
/// The optional predicate is evaluated for each otherwise legal recipient, allowing
/// callers to apply spell-reception rules (immunity/resistance) without duplicating
/// Plague's infection, adjacency, or ordering rules. An empty predicate accepts all.
DLL_LINKAGE std::optional<uint32_t> selectNextSpreadTarget(
	const CBattleInfoCallback & battle,
	const battle::Unit * afflicted,
	const std::function<bool(const battle::Unit *)> & recipientAllowed = {},
	const std::set<uint32_t> & excluded = {});

/// Applies Plague's ordinary negative magical-immunity/reception checks for a
/// prospective spread recipient (including invincibility, spell/school/level
/// immunity, and Spell Lock), but deliberately does not enforce the initial
/// cast's smart/enemy-side restriction. It does not roll probabilistic magic
/// resistance.
DLL_LINKAGE bool isSpreadRecipientReceptive(
	const CBattleInfoCallback & battle,
	BattleSide casterSide,
	const battle::Unit * recipient);

/// Canonical raw damage for one Plague tick, before target resistance or reduction.
/// School coefficient scales only the 0.8 x raw Spell Power term; basis points use
/// 10,000 as 100%. Thus Spell Power 100 at 100% coefficient produces 105 damage.
DLL_LINKAGE int64_t rawTickDamage(int32_t rawSpellPower, int32_t coefficientBasisPoints);

/// Applies the ordinary magical spell-damage pipeline to a stored Plague tick.
/// The saved Spell Power coefficient is already reflected in rawDamage; this
/// applies target mitigation and caster-side spell damage modifiers at tick time.
DLL_LINKAGE int64_t adjustedTickDamage(
	const CBattleInfoCallback & battle,
	BattleSide casterSide,
	const battle::Unit * target,
	int64_t rawDamage,
	const JsonNode * capturedPenetration = nullptr);
}
