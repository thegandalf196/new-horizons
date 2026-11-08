/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MoraleActivationValue.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/MoraleSuppressionState.h"
#include "../../lib/battle/Unit.h"
namespace moraleActivationValue
{
float expectedMoraleActivationChange(int morale)
{
	if(morale == 0) return 0;
	const auto settings = LIBRARY->engineSettings();
	const auto & chances = settings->getVector(morale > 0
		? EGameSettings::COMBAT_GOOD_MORALE_CHANCE : EGameSettings::COMBAT_BAD_MORALE_CHANCE);
	const int dice = settings->getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE);
	if(chances.empty() || dice <= 0) return 0;
	const auto index = std::min<size_t>(static_cast<size_t>(std::abs(morale)), chances.size()) - 1;
	const auto chance = std::max<int64_t>(0, chances[index]);
	return (morale > 0 ? 1.0f : -1.0f) * static_cast<float>(chance) / static_cast<float>(dice);
}
float expectedMoraleActivationChange(const CBattleInfoCallback & board, const battle::Unit * unit)
{
	if(!unit || !unit->alive() || unit->unaffectedByMorale()) return 0;
	return expectedMoraleActivationChange(board.battleGetMorale(unit));
}
float projectDelta(float before, float after, float horizon, MoraleSuppressionState & suppression)
{
	const float delta = after - before;
	if(!suppression.consume(before < 0.0f || after < 0.0f)) return delta * horizon;
	const float protectedDelta = std::max(0.0f, after) - std::max(0.0f, before);
	return delta * horizon + (protectedDelta - delta) * std::min(1.0f, horizon);
}
}
