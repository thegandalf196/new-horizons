/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicEntry.h"
#include "NewHorizonsMetamagicMarks.h"
#include "../battle/BattleInfo.h"
#include "../battle/CUnitState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../CStack.h"

namespace newHorizonsMetamagic
{
std::optional<Bonus> entryAttunement(const CBattleInfoCallback & callback, const battle::Unit & unit)
{
	const auto side = callback.playerToSide(callback.battleGetOwner(&unit));
	if(!callback.getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return std::nullopt;
	return entryAttunement(callback, unit, callback.getBattle()->getMetamagicPointState(side).points);
}

std::optional<Bonus> entryAttunement(const CBattleInfoCallback & callback,
	const battle::Unit & unit, int32_t points)
{
	const auto side = callback.playerToSide(callback.battleGetOwner(&unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return std::nullopt;
	const auto facts = profile(callback.battleGetFightingHero(side));
	const auto amount = attunementInitiative(facts, points);
	if(amount == 0 || !unit.alive() || unit.isGhost())
		return std::nullopt;
	Bonus initiative(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_FLAT,
		BonusSource::SECONDARY_SKILL, amount,
		BonusSourceID(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic"))));
	initiative.stacking = "new-horizons:metamagic.arcaneAttunement";
	initiative.subtype = BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(side)));
	initiative.spellCasterOwner = callback.sideToPlayer(side);
	return initiative;
}

void advanceRound(BattleInfo & battle)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto facts = profile(battle.getSideHero(side));
		if(!facts.pointPolicy || facts.rank == 0)
			continue;
		auto state = battle.getMetamagicPointState(side);
		state.points = pointsAtRoundStart(facts, state.points, battle.getRound());
		state.echoUsedThisRound = false;
		if(facts.arcaneSingularity && state.points >= 10)
			state.singularityReached = true;
		battle.setMetamagicPointState(side, state);
	}
}

void captureInitialMarks(BattleInfo & battle)
{
	const std::array<Profile, 2> profiles{profile(battle.getSideHero(BattleSide::ATTACKER)),
		profile(battle.getSideHero(BattleSide::DEFENDER))};
	for(auto & unit : battle.stacks)
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			unit->arcaneMarks.set(side, initialMarks(profiles.at(static_cast<size_t>(side))));
}

void seedSummonedMarks(battle::CUnitState & state, const std::array<const CGHeroInstance *, 2> & heroes)
{
	// Entry never copies an original stack's history. A non-summoned new entity
	// starts empty; Perfect Imprint applies only to the initial battlefield.
	state.arcaneMarks = {};
	if(!state.summoned)
		return;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		state.arcaneMarks.set(side, summonedMarks(profile(heroes.at(static_cast<size_t>(side)))));
}
}
