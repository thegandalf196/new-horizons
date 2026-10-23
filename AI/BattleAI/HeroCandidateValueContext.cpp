/*
 * HeroCandidateValueContext.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCandidateValueContext.h"
#include "PossibleSpellcast.h"
#include <bit>
#include <tuple>
namespace heroCandidateValueContext
{
namespace
{
std::optional<uint32_t> bits(const std::optional<float> & value)
{
	return value ? std::optional(std::bit_cast<uint32_t>(*value)) : std::nullopt;
}

// Every PossibleSpellcast option except destination and finalized value. Only
// the certified receipt permits ignoring original aim; it is never overwritten.
auto options(const PossibleSpellcast & c)
{
	return std::tuple(c.command, c.focusFire, c.commandTargets,
		std::bit_cast<uint32_t>(c.commandHeuristicValue), c.spell, c.spellOvercharge,
		c.spellSelectiveDispel, c.spellCureAffliction, c.spellCurePhysicalAffliction,
		c.spellPurifyChoices, c.spellPurifyPhysicalTargets,
		std::bit_cast<uint32_t>(c.spellPurifyHeuristicValue), c.spellMassSlow,
		c.spellShadowGiftSacrificePercent, c.spellStormOfDaggers, c.metamagicFollowup,
		c.metamagicGrand, c.divineMandateFollowup,
		std::bit_cast<uint32_t>(c.spellPlacementHeuristicValue),
		bits(c.spellHandOfFateExpectedValue), bits(c.spellBattleFormExpectedValue),
		bits(c.spellPuppetMasterExpectedValue), bits(c.spellConfusionExpectedValue),
		std::bit_cast<uint32_t>(c.spellNaturePoisonValue),
		std::bit_cast<uint32_t>(c.spellSoulChainDelayedValue),
		std::bit_cast<uint32_t>(c.spellShadowGiftHeuristicValue), c.spellFireWallDirection);
}

}
bool sameOptions(const PossibleSpellcast & a, const PossibleSpellcast & b)
{
	return options(a) == options(b);
}
bool sameCounterspell(const HypotheticBattle::ProjectedCounterspellOutcome & a,
	const HypotheticBattle::ProjectedCounterspellOutcome & b)
{
	return std::tie(a.wardSide, a.wardActive, a.resolutionKnown, a.negated, a.manaCost)
		== std::tie(b.wardSide, b.wardActive, b.resolutionKnown, b.negated, b.manaCost);
}

}
