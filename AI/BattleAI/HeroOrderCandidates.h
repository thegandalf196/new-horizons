/*
 * HeroOrderCandidates.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "PossibleSpellcast.h"
#include "../../lib/battle/BattleSide.h"

#include <memory>

class CBattleCallback;
class CPlayerBattleCallback;

namespace heroOrderCandidates
{
/// Call-local views; the scorer cannot retain these references beyond score().
struct ValuationRequest
{
	const std::shared_ptr<CPlayerBattleCallback> & battle;
	BattleSide side;
	HeroCommand command;
	const std::vector<uint32_t> & targets;
	std::optional<int> focusFirePercent;
};

/// Nonowning strategy: one stack adapter, with no allocated callable wrapper.
class Valuation
{
public:
	virtual ~Valuation() = default;
	virtual float score(const ValuationRequest & request) const = 0;
};

/// Append after spells, in historical command/target order. Availability and
/// target validation remain callback queries; this component never submits actions.
void appendOrders(std::vector<PossibleSpellcast> & possibleCasts,
	CBattleCallback & callback, BattleID battleID, BattleSide side,
	bool mandatoryOrder, const Valuation & valuation);
}
