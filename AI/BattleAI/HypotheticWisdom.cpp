/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/entities/hero/NewHorizonsInfernoSpecialties.h"

void HypotheticBattle::captureKindlingHistory(const CBattleInfoCallback & source)
{
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(&source))
	{
		kindlingHistories = parent->kindlingHistories;
		return;
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(newHorizonsHeroes::usesKindlingHistory(source.battleGetFightingHero(side)))
			kindlingHistories.at(side) = std::make_shared<const std::vector<SpellID>>(source.getBattle()->getUsedSpells(side));
}

std::vector<SpellID> HypotheticBattle::getUsedSpells(BattleSide side) const
{
	if(const auto & history = kindlingHistories.at(side)) return *history;
	// Preserve the original history provider for subjects without Kindling.
	return subject->getBattle()->getUsedSpells(side);
}

const WisdomCombatState & HypotheticBattle::getWisdomCombatState(BattleSide side) const
{
	static const WisdomCombatState unknown;
	const auto & state = forecastSide(side);
	return state->ledger ? state->ledger->wisdom : unknown;
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
	const auto receipt = forecast::SideTransitions::recordWisdom(owner->forecastSide(side), spell, round, owner->searchBudget().get());
	if(!receipt || !owner->commitForecastSide(side, *receipt)) throw std::invalid_argument("Unknown projected Wisdom owner");
	if(!automatic)
	{
		if(const auto & history = owner->kindlingHistories.at(side))
		{
			auto next = std::make_shared<std::vector<SpellID>>(*history);
			next->push_back(spell);
			owner->kindlingHistories.at(side) = std::move(next);
		}
		if(begun) owner->recordedWisdomActionEpochs.at(side) = begun->action.epoch;
	}
}
