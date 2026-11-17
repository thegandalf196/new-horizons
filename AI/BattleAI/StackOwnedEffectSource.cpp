/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "ForecastEffectState.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include <algorithm>
#include <stdexcept>
std::optional<std::vector<std::shared_ptr<const Bonus>>> StackWithBonuses::ownedLocalOtherEffects() const
{
	if(!modifierBaseProgram) return std::nullopt;
	if(!modifierBaseEffects) throw std::logic_error("Owned local clocks have no source receipt");
	const auto query = currentModifierQueries()->getUnstackedBonuses(Selector::all);
	std::vector<bool> consumed(query->size(), false);
	std::vector<std::shared_ptr<const Bonus>> result;
	const auto retain = [&](std::shared_ptr<const Bonus> raw)
	{
		if(!raw || raw->source == BonusSource::SPELL_EFFECT) return;
		if(!newHorizonsFrozen::markerApplicationRound(*raw)
			&& !(raw->source == BonusSource::OTHER && raw->duration == BonusDuration::STACK_GETS_TURN)) return;
		for(size_t index = 0; index < query->size(); ++index)
			if(!consumed[index] && battle::exactBattleEffectEqual(*raw, *(*query)[index]))
			{
				consumed[index] = true;
				result.push_back(std::move(raw)); // the trusted RAW owner, never an effective row
				return;
			}
	};
	// Root/child sealing retained exact local raw ownership. The materialized
	// frame is only a membership check; it cannot invent or supply a clock.
	for(const auto & record : modifierBaseEffects->values()) retain(record.value);
	for(const auto * rows : {&bonusesToAdd, &bonusesToUpdate})
		for(const auto & raw : *rows) retain(raw);
	return result;
}
