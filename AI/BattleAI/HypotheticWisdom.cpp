/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"

const WisdomCombatState & HypotheticBattle::getWisdomCombatState(BattleSide side) const
{
	return wisdomCombatStates.at(side);
}

void HypotheticBattle::HypotheticServerCallback::recordCompletedWisdomSpellCast(
	BattleSide side, const SpellID & spell, int32_t round, bool automatic)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| round != owner->getRound())
		throw std::invalid_argument("Invalid hypothetical Wisdom cast receipt");
	const auto & begun = owner->begunProjectedSpellAllowances.at(side);
	if(!automatic && begun
		&& (begun->spell != spell || begun->action.epoch != owner->projectedActionEpochs.at(side)))
		throw std::invalid_argument("Wisdom cast does not match the begun projected action");
	if(!automatic && begun && owner->recordedWisdomActionEpochs.at(side) == begun->action.epoch)
		throw std::logic_error("Wisdom cast was already recorded for this projected action");
	owner->wisdomCombatStates.at(side).recordCompletedCast(spell, round);
	if(!automatic && begun)
		owner->recordedWisdomActionEpochs.at(side) = begun->action.epoch;
}
