/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveDivineMandate.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include <stdexcept>
#include "OwnedUnitEnvironmentPolicy.h"
#include "LiveUnitEnvironmentContext.h"
#include "UnitOwnershipQuery.h"
#include "Unit.h"
namespace battle
{
OwnedUnitEnvironmentPolicy::OwnedUnitEnvironmentPolicy(const CBattleInfoCallback & observer)
{
	const LiveUnitEnvironmentContext source(observer); // The same real registered callback perspective.
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		for(const auto & order : observer.getBattle()->getHeroOrderStates(side))
			if(order.passiveDivineMandate && order.passiveDivineMandate->has(
				newHorizonsPassiveDivineMandate::Perk::RIGHTEOUS_DISCIPLINE))
				throw std::invalid_argument("Owned environment does not capture active Righteous Discipline Orders");
		if(newHorizonsPassiveDivineMandate::capturePolicy(observer.battleGetFightingHero(side)).has(
			newHorizonsPassiveDivineMandate::Perk::RIGHTEOUS_DISCIPLINE))
			throw std::invalid_argument("Owned environment does not capture dynamic Righteous Discipline Orders");
		auto & input = inputs.at(side);
		input.color = source.ownership().sidePlayer(side);
		input.ammoArtifact = source.sideHasAmmoArtifact(side);
		input.veteranCohesion = source.sideHasVeteranCohesion(side);
		input.necromancy = source.necromancyPassives(side);
		input.elementalAttunementProfile = source.elementalAttunementProfile(side);
		input.elementalAttunementProfileKnown = source.knowsElementalAttunementProfile(side);
		input.bloodrageRank = source.bloodrageRank(side);
		input.bloodrageCap = source.bloodrageCap(side);
		input.bloodrageSpeed = source.bloodrageSpeed(side);
		input.bloodrageRetaliations = source.bloodrageRetaliations(side);
		input.bloodragePain = source.bloodragePain(side);
		auto & state = states.at(side); state.fortune = source.fortune(side);
		state.bloodrageDamagePercent = source.bloodrageDamage(side);
		state.elementalAttunementMask = source.elementalAttunementMask(side);
	}
	committedElementalEffects = source.committedElementalEffects();
	for(const auto * unit : observer.battleGetAllUnits(false))
		warfareStatBonuses.emplace(unit->unitId(), observer.battleWarfareStatBonus(unit));
}
std::shared_ptr<const OwnedUnitEnvironmentPolicy> OwnedUnitEnvironmentPolicy::capture(const CBattleInfoCallback & observer)
{
	// Only the real factory invokes this during state/root/native admission.
	// Query dependencies stay private; UNKNOWN HeroKnowledge remains a separate
	// observer value and can never be replaced by these compatibility answers.
	return std::shared_ptr<const OwnedUnitEnvironmentPolicy>(new OwnedUnitEnvironmentPolicy(observer));
}
OwnedUnitEnvironmentPolicy::~OwnedUnitEnvironmentPolicy() = default;
}
