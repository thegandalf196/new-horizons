/*
 * HeroActionSelectionIdentity.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "PossibleSpellcast.h"
#include <bit>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
namespace heroActionSelection
{
/// Call-local observer values only. Spell identity is carried by CandidateValue;
/// original unit pointers never cross this observation boundary.
struct TargetIdentity
{
	std::optional<uint32_t> unitId;
	BattleHex hex;
	bool operator==(const TargetIdentity &) const = default;
};
inline std::optional<uint32_t> optionBits(const std::optional<float> & value)
{
	return value ? std::optional(std::bit_cast<uint32_t>(*value)) : std::nullopt;
}

// Immutable observation of every option. Spell ID and ordered destinations
// are carried separately; float payloads retain their exact bit representation.
inline auto candidateOptions(const PossibleSpellcast & c)
{
	return std::tuple(c.command, c.focusFire, c.commandTargets,
		std::bit_cast<uint32_t>(c.commandHeuristicValue), c.spellOvercharge,
		c.spellSelectiveDispel, c.spellCureAffliction, c.spellCurePhysicalAffliction,
		c.spellPurifyChoices, c.spellPurifyPhysicalTargets,
		std::bit_cast<uint32_t>(c.spellPurifyHeuristicValue), c.spellMassSlow,
		c.spellShadowGiftSacrificePercent, c.spellStormOfDaggers, c.metamagicFollowup,
		c.metamagicGrand, c.divineMandateFollowup,
		std::bit_cast<uint32_t>(c.spellPlacementHeuristicValue),
		optionBits(c.spellHandOfFateExpectedValue), optionBits(c.spellBattleFormExpectedValue),
		optionBits(c.spellPuppetMasterExpectedValue), optionBits(c.spellConfusionExpectedValue),
		std::bit_cast<uint32_t>(c.spellNaturePoisonValue),
		std::bit_cast<uint32_t>(c.spellSoulChainDelayedValue),
		std::bit_cast<uint32_t>(c.spellShadowGiftHeuristicValue), c.spellFireWallDirection);
}

using OptionIdentity = decltype(candidateOptions(std::declval<const PossibleSpellcast &>()));
struct CandidateIdentity
{
	std::vector<TargetIdentity> targets;
	OptionIdentity options;
	bool operator==(const CandidateIdentity &) const = default;
};
static_assert(std::is_nothrow_move_constructible_v<CandidateIdentity>);
static_assert(std::is_nothrow_move_assignable_v<CandidateIdentity>);
}
