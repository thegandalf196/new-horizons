/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "SpellEffectPreview.h"
namespace spells { class Mechanics; }
namespace battle { class Unit; }
namespace newHorizonsMagic
{
struct DLL_LINKAGE RecipientSpellEffectPreview
{
	SpellEffectPreview effect;
	/// Positive round timer only; absent for instantaneous/permanent/non-round effects.
	std::optional<int32_t> durationRounds;
};
/// Shared authored fixed timers after native cast extensions, before recipient Meta.
/// Only explicitly owned script families have a value. Does not search or simulate.
DLL_LINKAGE std::optional<int32_t> scriptedRoundStatusDuration(const spells::Mechanics & mechanics);
/// Exact existing producer quantities. Unsupported families return no preview.
/// No world copy, castEval, RNG, mutation, target enumeration or fabricated duration.
DLL_LINKAGE std::optional<RecipientSpellEffectPreview> recipientSpellEffectPreview(
	const spells::Mechanics & mechanics, const battle::Unit * recipient);
}
