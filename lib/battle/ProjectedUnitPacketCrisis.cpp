/*
 * ProjectedUnitPacketCrisis.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "CrisisCommandReferences.h"

namespace
{
class PacketCrisisReferences final : public newHorizonsCrisisCommand::CrisisCommandReferences
{
	const ProjectedBonusRoster & roster;
	const BattleSideArray<const CGHeroInstance *> & heroes;
	const SeizeInitiativeState & seize;
	const BattleSideArray<HeroActionAllowanceState> & allowances;
	int32_t round;
public:
	PacketCrisisReferences(const ProjectedBonusRoster & roster,
		const BattleSideArray<const CGHeroInstance *> & heroes, const SeizeInitiativeState & seize,
		const BattleSideArray<HeroActionAllowanceState> & allowances, int32_t round)
		: roster(roster), heroes(heroes), seize(seize), allowances(allowances), round(round) {}
	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override
	{
		battle::Units result;
		for(const auto * unit : roster.orderedUnits())
			if(predicate(unit))
				result.push_back(unit);
		return result;
	}
	const CGHeroInstance * getSideHero(BattleSide side) const override { return heroes.at(side); }
	const SeizeInitiativeState & getSeizeInitiativeState() const override { return seize; }
	int32_t getRound() const override { return round; }
	const HeroActionAllowanceState & getHeroActionAllowances(BattleSide side) const override { return allowances.at(side); }
};
}

void battle::ProjectedBattleWorldArena::Data::validateCrisisForStorage(
	const newHorizonsCrisisCommand::State & state) const
{
	const PacketCrisisReferences references(roster, sideHeroes, seize, allowances, round);
	newHorizonsCrisisCommand::validateCrisisState(state, references);
}

void battle::ProjectedBattleWorldArena::Data::recordCrisisDeaths(
	const std::vector<newHorizonsCrisisCommand::DeathReceipt> & candidates)
{
	auto next = crisis;
	for(const auto & receipt : candidates)
		if(const auto * slot = find(receipt.unit, false); slot && !slot->state->alive())
			next.capture(receipt.side, receipt.unit);
	validateCrisisForStorage(next);
	const auto nextSeize = next.seizeContextAfterTransition(crisis, seize);
	// capture changes pending/used only: returns and active actor stay unchanged.
	crisis = next;
	seize = nextSeize;
}

const newHorizonsCrisisCommand::State & battle::ProjectedBattleWorldArena::crisisState() const
{
	impl->requireUsable();
	return impl->crisis;
}
