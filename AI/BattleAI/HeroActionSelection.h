/*
 * HeroActionSelection.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <cstddef>
#include <optional>
#include <span>

class PossibleSpellcast;

namespace heroActionSelection
{
enum class Disposition { RETAIN_FOLLOWUP, PERFORM, USELESS };
struct CandidateValue
{
	size_t ordinal;
	HeroCommand command;
	SpellID spell;
	int32_t overcharge;
	float value;
};
struct SelectionValue
{
	size_t ordinal;
	float baseline;
	Disposition disposition;
};
class Observer
{
public:
	virtual ~Observer() = default;
	virtual void candidate(CandidateValue value) noexcept = 0;
	virtual void selected(SelectionValue value) noexcept = 0;
};
struct Selection
{
	size_t ordinal;
	Disposition disposition;
};

/// Same cached-score sentinel test; no new baseline or arithmetic grouping.
float noCastBaseline(float cachedScore);
/// Existing roster order, strict first maximum, then unchanged admission.
/// Empty input returns nullopt without callbacks. Observer is call-local, not
/// retained; only value copies cross its boundary. Null observer allocates/logs
/// nothing and never requests clocks, callbacks, state, mechanics or native RNG.
std::optional<Selection> choose(std::span<const PossibleSpellcast> candidates,
	float baseline, bool mandatoryOrder, Observer * observer = nullptr);
}
