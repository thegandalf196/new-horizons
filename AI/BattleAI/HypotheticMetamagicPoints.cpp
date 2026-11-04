/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/spells/NewHorizonsMetamagicMarks.h"

const MetamagicPointState & HypotheticBattle::getMetamagicPointState(BattleSide side) const
{
	return metamagicPointStates.at(side);
}

void HypotheticBattle::setMetamagicPointState(BattleSide side, const MetamagicPointState & state)
{
	state.validate();
	metamagicPointStates.at(side) = state;
}

void HypotheticBattle::advanceMetamagicPoints()
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto facts = newHorizonsMetamagic::profile(battleGetFightingHero(side));
		if(!facts.pointPolicy || facts.rank == 0)
			continue;
		auto state = getMetamagicPointState(side);
		state.points = newHorizonsMetamagic::pointsAtRoundStart(facts, state.points, getRound());
		state.echoUsedThisRound = false;
		if(facts.arcaneSingularity && state.points >= 10)
			state.singularityReached = true;
		setMetamagicPointState(side, state);
	}
}

void HypotheticBattle::restoreProjectedNormalMana(const SetMana & packet)
{
	packet.validateOperationPayload();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battleGetFightingHero(side);
		if(!hero || hero->id != packet.hid)
			continue;
		auto & mana = projectedMana.at(side);
		if(!mana)
			mana = ProjectedMana{*getProjectedMana(side), getTemporaryBufferRemaining(side)};
		mana->pools.restoreNormal(static_cast<int32_t>(std::min<int64_t>(packet.amount,
			std::numeric_limits<int32_t>::max())), hero->manaLimit());
		return;
	}
	throw std::invalid_argument("Projected Mana recovery requires a fighting hero");
}
